# MARE dataset generator

Builds a training dataset of lunar rover camera frames with pixel-accurate ground
truth: the rover drives autonomously through procedurally generated terrains
under varied sun positions, and every kept frame becomes a sample with stereo
RGB images, depth, object and shadow labels, event and SPAD images, and the
rover pose,
plus the lidar scan and its projection into the camera as sparse depth.

Everything is driven by [`config.yaml`](config.yaml) and one seed: the same
config regenerates the same terrains, rock fields, spawn points, goals and sun
positions. The simulation itself is not bit-exact between runs (physics and
rendering), so individual frames differ slightly from one run to the next.

**You do not need this folder to use the dataset.** The published
[MARE Lunar Rover Dataset](https://huggingface.co/datasets/raulcastar/mare-lunar-rover-dataset)
(5,040 samples, CC BY 4.0) was made with [`config_v2.yaml`](config_v2.yaml) and
can be downloaded directly; baselines trained on it are in
[MARE-Dataset-Baselines](https://github.com/raulcastar/MARE-Dataset-Baselines).
This folder is for regenerating it or recording a new one.

## Quick start

With the [requirements](#requirements) in place (Unreal open on the dataset
level, the GNC stack running, Terrain Studio next to this repository):

```bash
cd dataset
pip install -r requirements.txt
python generate.py all --config config_v2.yaml      # maps, plan and record; resumes if interrupted
python generate.py status --config config_v2.yaml   # progress at any time
python export_hf.py output_v2 output_v2_hf          # optional: pack it as on Hugging Face
```

`config_v2.yaml` (seed 2027) reproduces the published dataset's terrains,
episodes and suns; it writes to `output_v2/`. On a laptop with an RTX 4060, the
whole run took about 6 hours: seconds per map, then about 8.5 minutes per
episode (3.6 hours of driving and recording for the 25 episodes) plus their
extraction.

## How it works

| Stage | Command | What it does | Needs |
|---|---|---|---|
| Maps | `python generate.py maps` | Terrain Studio generates each terrain (craters, relief, rock field with connected rover corridors, crater mask, Landscape heightmap) | Python only |
| Plan | `python generate.py plan` | Draws each episode: a spawn on a corridor, a heading, a chain of goals, a sun azimuth and elevation → `plan.json` | Python only |
| Record | `python generate.py record` | For each map, rebuilds the dataset level in Unreal (Landscape, rocks, crater mask) and points the GNC planner at its maps; for each episode, sets the sun, places the rover, presses Play, drives the goals autonomously while recording a rosbag, then extracts the samples and keeps the bag without its images | Unreal + GNC stack |
| Status | `python generate.py status` | Maps built, episodes recorded, samples so far | — |

`python generate.py all` runs the three stages. Every stage resumes where it
stopped; `record` stops once `target_samples` samples exist.

The scripts:

| File | Role |
|---|---|
| `generate.py` | The stages above |
| `dataset_maps.py` | Terrains and rock fields with Terrain Studio's own modules |
| `dataset_unreal.py` | The dataset level in Unreal: map swap, sun, rover placement, Play |
| `dataset_gnc.py` | The GNC stack: planner maps, goals, rosbag recording (hard time limit) |
| `extract.py` | Rosbag → samples (also `python extract.py` for bags left unextracted) |
| `align_labels.py` | The central labels and depth resampled onto the SPAD and event grids (run by the extractor) |
| `export_hf.py` | Packs a generated dataset as WebDataset shards, split by map, for the Hugging Face Hub (nothing is uploaded) |

## Requirements

- This project open in Unreal Engine 5.7 on the **dataset level**, a copy of
  `Lunar_Terrain_Raph` saved as `/Game/Dataset/Dataset_Raph` (*File → Save
  Current Level As*), not in Play, with *Remote Execution* on. The generator
  replaces that level's Landscape and rocks map by map and saves it; it checks
  the open level before every change and touches no other level.
- [GNC-Moon-Simulator](https://github.com/raulcastar/GNC-Moon-Simulator) running
  (`./docker_build.sh`). The planner is pointed at each map at runtime; nothing
  is rebuilt.
- [Unreal-Moon-Terrain-Generation](https://github.com/raulcastar/Unreal-Moon-Terrain-Generation)
  (Terrain Studio) cloned next to this repository, or its path in
  `paths.terrain_studio`.
- Python 3 with `pip install -r requirements.txt` and Terrain Studio's requirements;
  on Linux, `./run_docker.sh <generate.py arguments>` runs it in Docker instead.
- Disk: about 5 MB per sample (about 25 GB for 5,000), plus up to a few GB for
  the bag being processed. Keep `output_dir` outside cloud-synced folders.

## The dataset

```
output/
  config_used.yaml, plan.json, log.txt
  maps/<map>/            terrain, rocks, crater mask, heightmap, map.json
  episodes/<episode>.json the episode's plan and how each goal ended
  bags/<episode>/<episode>_no_images.mcap  the episode's rosbag without images or lidar scans: odometry, tf, IMU,
                           clock, mission state, camera_info, class legends (frames.keep_bags)
  samples/<episode>/
    rgb_central/000000.png   RGB, 640 x 480, the camera the labels belong to
    rgb_left/000000.png      left stereo navcam, same instant
    rgb_right/000000.png     right stereo navcam, same instant
    depth/000000.png         uint16, millimetres along the central camera's view; 0 = no depth (sky, > 65.5 m)
    depth_m/000000.npy       float32, the same depth in metres, unrounded; inf = sky (frames.depth_npy)
    object/000000.png        uint8 object class id per pixel (legend in episode.json)
    shadow/000000.png        uint8 shadow class id per pixel (legend in episode.json)
    events/000000.png        event-camera frame within 0.1 s, when one exists
    spad/000000.png          SPAD image within 0.1 s, when one exists
    lidar/000000.npy         lidar scan within 0.5 s (1 Hz lidar): float32 (rings, columns, 4) = x y z
                             intensity in the lidar frame (x forward, y left, z up); NaN = no return
    lidar_depth/000000.png   that scan projected into the central camera: uint16 mm, 0 = no point
    lidar_depth_m/000000.npy the same in float32 metres (0 = no point); corrected for the rover's
                             motion between the scan and the frame (odometry)
    preview/                 for looking at, not training: depth/ and lidar_depth/ (JET, log scale
                             0.5-60 m, near red; lidar dots over the dimmed RGB), object/ (class
                             colours), shadow/ (the dashboard's colours: sunlit blue, shadow black,
                             objects grey, rover shadow orange)
    rgb_central_noglare/     v2: the central image without bloom or lens flares (same rendered frame)
    rear_rgb/                v2: rear camera (looking back, 90 deg FOV) within 0.1 s; sees the wheel tracks
    rear_object/, rear_shadow/  v2: its object class ids and shadow class ids (same rendered frame as rear_rgb)
    spad_object/, spad_shadow/, spad_depth_m/        the central labels and depth on the SPAD's 256 x 256 grid
    events_object/, events_shadow/, events_depth_m/  the same on the event camera's 346 x 260 grid
                             (255 / NaN = outside the central camera; see "SPAD and event labels")
    frames.csv               frame, stamp (s), event/SPAD/lidar (v2: noglare/rear) stamps, rover pose
                             x y z qx qy qz qw (map frame)
    episode.json             map, sun, spawn, goal results, camera intrinsics, class legends
```

- **Object classes:** sky, sun, sun glare, landscape, crater, crater rim,
  wheel track, boulder, small rock, rover (ids in `episode.json`,
  `object_classes`).
- **Shadow classes:** 0 sky, 1 sunlit ground, 2 shadowed ground, 3 sunlit
  object, 4 shadowed object, 5 the rover's own shadow.
- **Map frame:** x and y in metres, x = DEM column and y = DEM row of the
  terrain in `maps/<map>/`; z up.
- **Frame selection:** a frame is kept once the rover has moved
  `min_spacing_m` or `max_gap_s` of simulation time has passed, so consecutive
  samples are not near-duplicates.
- **Event and SPAD images** are paired with the navcam frame when one lies
  within `pair_tolerance_s`. The generator sets both GNC nodes to 10 Hz
  (`gnc.node_params`), above the navcams' rate, so nearly every frame has both;
  at their former 1 Hz only about one frame in five had them.
- **SPAD and event labels:** on Leo_Raph the SPAD and the event camera sit at
  the central navcam's position and orientation with the same 90 degree
  horizontal field of view, only at lower resolution, so the central camera's
  object ids, shadow ids and planar depth map onto their pixels exactly
  (`align_labels.py`, run by the extractor; `python align_labels.py <folder>`
  for an existing dataset). The SPAD is square (90 x 90 degrees): its rows
  outside the central camera's 74 degree height, above row 32 and below row
  223, have no label (255) and NaN depth. The labels belong to the central
  frame; the SPAD and event images were taken within `pair_tolerance_s` of it
  (their stamps are in `frames.csv`).
- **Lens flares** are kept in the RGB images (a camera effect); they are not in
  the labels, which describe the scene.
- **Rocks without collision, no re-planning** (by default): the rover drives
  its planned route straight through the rocks (`unreal.rock_collision: false`)
  and the planner ignores lidar obstacles (`gnc.obstacle_avoidance: false`), so
  episodes do not stall in rock fields. The rocks stay rendered and labelled.
  Set both to `true` for physical rock interaction and lidar re-planning.

Loading a sample:

```python
import json, numpy as np
from PIL import Image

ep = "output/samples/map_000_ep00"
rgb = np.array(Image.open(f"{ep}/rgb_central/000010.png"))
depth_m = np.load(f"{ep}/depth_m/000010.npy")                         # metres, inf = sky
objects = np.array(Image.open(f"{ep}/object/000010.png"))
classes = {c["id"]: c["name"] for c in json.load(open(f"{ep}/episode.json"))["object_classes"]}
```

## Versions

| | `config.yaml` (v1) | `config_v2.yaml` (v2) |
|---|---|---|
| Cameras | navcams (central, left, right) | + rear camera with its object and shadow labels, + glare-free central image |
| Craters per map | 100-190 (`CRATER_DENSITY` 0.0003-0.0008) | 300-620, smaller (0.0012-0.0025, max radius 3-8 % of the map) |
| Rocks | presets as they are | `ROCK_COVERAGE` 2-5 % (`maps.rock_vary`) |
| Spawns and goals | uniform on the rover corridors | weighted towards rocks and craters (`episodes.feature_bias`) |

v1 has rocks and craters in only about 2 % of the pixels and wheel tracks only
where the rover turns back across its path; v2 raises the first and its rear
camera sees the tracks all the time. v2 needs the UnrealStereoCam plugin with
`bStreamCenterNoGlare` (the generator switches it on in the dataset level,
`unreal.streamer`) and sets the GNC stereo node's `extra_cameras` itself
(`gnc.node_params`).

## Changing the dataset

Edit `config.yaml`: number and size of maps, the ranges their parameters are
drawn from, rock presets, episodes per map, goals, sun ranges, frame spacing and
the target size. Use a new `output_dir` (or `--output`) for a new dataset; a
changed config in an existing folder mixes the two.
