"""
Builds the dataset's terrains offline with Terrain Studio's own modules (the code
its GUI runs): a DEM with craters, a rock field with connected rover corridors,
the crater mask for the object labels and the Landscape heightmap, all under
<output>/maps/<name>/. No Unreal Engine needed.
"""
import json
import os
import sys

import numpy as np


def load_studio(studio):
    """Puts Terrain Studio's modules on the path and returns them."""
    root = os.path.join(studio, "Terrain_Studio")
    for path in (root, os.path.join(root, "generator"), os.path.join(root, "unreal_link")):
        if path not in sys.path:
            sys.path.insert(0, path)
    import crater_map
    import heightmap_files
    import rock_field
    import send_terrain
    import terrain_core
    return {"crater_map": crater_map, "heightmap_files": heightmap_files, "rock_field": rock_field,
            "send_terrain": send_terrain, "terrain_core": terrain_core}


def slope_deg(dem):
    gy, gx = np.gradient(np.asarray(dem, dtype=float))
    return np.degrees(np.arctan(np.hypot(gx, gy)))


def build_map(studio, maps_dir, name, size_m, terrain_preset, overrides, terrain_seed,
              rock_preset, rock_seed, log=print, rock_overrides=None):
    """
    Generates one map. Returns its metadata (also written to <name>/map.json), or
    None when the rock planner could not connect the corridors (try another seed).
    """
    m = load_studio(studio)
    tc, rf, cm = m["terrain_core"], m["rock_field"], m["crater_map"]

    params = tc.load_config(os.path.join(studio, "config", f"{terrain_preset}.yaml"))
    params.update(overrides)
    params["ROWS"] = params["COLS"] = int(size_m)
    params["MACRO_ELEVATION_MIN"] = min(params["MACRO_ELEVATION_MIN"], params["MACRO_ELEVATION_MAX"])
    params = tc.coerce_params(params)
    result = tc.generate_terrain(params, seed=terrain_seed)
    tc.export_outputs(result["elevation"], maps_dir, base_name=f"{name}_elevationMap",
                      export_csv=True, export_npy=True, craters=result["craters"], params=params)
    group_dir = os.path.join(maps_dir, name)
    dem = result["elevation"]

    # Rock field and corridors, around the exact craters the generator placed
    craters_csv = os.path.join(group_dir, f"{name}_craters.csv")
    rock_params = rf.load_preset(os.path.join(studio, "config", "rocks", f"{rock_preset}.yaml"))
    rock_params.update(rock_overrides or {})
    rock_params = rf.coerce_params(rock_params)
    field = rf.build_rock_field(dem, 1.0, rock_params, seed=rock_seed,
                                craters=cm.load_crater_list(craters_csv, with_depth=True))
    if not field["connected"]:
        log(f"  {name}: corridors not connected with rock seed {rock_seed}")
        return None
    rf.export_rock_field(field, maps_dir, f"{name}_elevationMap")

    # Crater mask (object labels), from the exact crater list
    craters = cm.load_crater_list(craters_csv)
    k = cm.supersample_for(dem.shape)
    labels = cm.render_labels(dem.shape, craters, cm.CRATER_ID, cm.RIM_ID, cm.RIM_FRACTION, k)
    mask = cm.export_crater_mask(labels, maps_dir, name, 1.0, 0.0, cm.CRATER_ID, cm.RIM_ID,
                                 cm.RIM_FRACTION, supersample=k, source="generator", craters=craters)

    # 16-bit Landscape heightmap
    stem = f"{name}_elevationMap"
    layout, resolution, z_scale, r16, z_offset = m["send_terrain"].generate_lunar_terrain(
        os.path.join(group_dir, f"{stem}.npy"), group_dir, 1.0)

    meta = {
        "name": name, "size_m": int(size_m), "terrain_preset": terrain_preset,
        "terrain_seed": int(result["seed"]), "terrain_overrides": overrides,
        "rock_preset": rock_preset, "rock_seed": int(rock_seed), "rock_overrides": rock_overrides or {},
        "craters": int(result["crater_count"]), "rocks": int(field["rock_count"]),
        "drivable_pct": round(100 * field["drivable_frac"], 1),
        "elevation_m": [round(float(np.min(dem)), 2), round(float(np.max(dem)), 2)],
        "landscape": {"layout": list(layout), "resolution": resolution, "z_scale": z_scale,
                      "r16": r16, "z_offset": z_offset, "label": f"{stem}_heightmap",
                      "width": m["heightmap_files"].heightmap_width(group_dir, stem)},
        "files": {
            "dem": os.path.join(group_dir, f"{stem}.npy"),
            "elevation_csv": os.path.join(group_dir, f"{stem}.csv"),
            "soil_csv": os.path.join(group_dir, f"{name}_soilMap.csv"),
            "rocks_csv": os.path.join(group_dir, f"{name}_rocks.csv"),
            "rover_paths_csv": os.path.join(group_dir, f"{name}_roverPaths.csv"),
            "crater_mask_png": mask["crater_mask_png"],
            "crater_mask_json": mask["crater_mask_json"],
        },
    }
    missing = [k for k, p in meta["files"].items() if not os.path.isfile(p)]
    if missing:
        raise RuntimeError(f"{name}: Terrain Studio did not write {missing}")
    with open(os.path.join(group_dir, "map.json"), "w") as f:
        json.dump(meta, f, indent=2)
    return meta


def corridor_points(meta, edge_margin_m, max_slope_deg):
    """Corridor points (map x, y in metres) away from the edges and on gentle ground."""
    data = np.loadtxt(meta["files"]["rover_paths_csv"], delimiter=",", skiprows=1, usecols=(1, 2), ndmin=2)
    dem = np.load(meta["files"]["dem"])
    slope = slope_deg(dem)
    rows, cols = dem.shape
    keep = []
    for x, y in data:
        c, r = int(round(x)), int(round(y))
        if not (edge_margin_m <= c < cols - edge_margin_m and edge_margin_m <= r < rows - edge_margin_m):
            continue
        if slope[max(r - 1, 0):r + 2, max(c - 1, 0):c + 2].max() > max_slope_deg:
            continue
        keep.append((float(x), float(y)))
    return np.array(keep)


def feature_score(meta, points, radius_m):
    """
    How much there is to see around each point (map x, y): rocks (boulders count
    3) and crater rim and floor cells within radius_m, from the map's own rock
    and crater lists. Same order as points.
    """
    from scipy.ndimage import uniform_filter
    dem = np.load(meta["files"]["dem"])
    grid = np.zeros(dem.shape)
    rows, cols = dem.shape
    rocks = np.loadtxt(meta["files"]["rocks_csv"], delimiter=",", skiprows=1, usecols=(1, 2, 5), ndmin=2)
    for r, c, scale in rocks:
        if 0 <= int(r) < rows and 0 <= int(c) < cols:
            grid[int(r), int(c)] += 3.0 if scale >= 0.5 else 1.0
    craters_csv = os.path.join(os.path.dirname(meta["files"]["dem"]), f"{meta['name']}_craters.csv")
    craters = np.loadtxt(craters_csv, delimiter=",", skiprows=1, usecols=(1, 2, 6), ndmin=2)
    yy, xx = np.mgrid[0:rows, 0:cols]
    for r, c, radius in craters:
        r0, r1 = int(max(r - 1.3 * radius - 1, 0)), int(min(r + 1.3 * radius + 2, rows))
        c0, c1 = int(max(c - 1.3 * radius - 1, 0)), int(min(c + 1.3 * radius + 2, cols))
        d = np.hypot(yy[r0:r1, c0:c1] - r, xx[r0:r1, c0:c1] - c)
        grid[r0:r1, c0:c1] += 0.2 * (d <= 1.3 * radius)       # a crater counts by its area, lightly
    score = uniform_filter(grid, size=2 * int(radius_m) + 1, mode="constant")
    idx_r = np.clip(np.round(points[:, 1]).astype(int), 0, rows - 1)
    idx_c = np.clip(np.round(points[:, 0]).astype(int), 0, cols - 1)
    return score[idx_r, idx_c]
