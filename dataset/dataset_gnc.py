"""
The GNC side of the dataset generator: mission-control API calls, pointing the
path planner at each map, and rosbag recording in the dashboard's container
(every recording has a hard time limit).
"""
import json
import math
import os
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request

FINAL_STATES = ("reached", "aborted", "cancelled")

# Recorded per episode; extract.py turns them into samples.
TOPICS = (
    "/clock", "/odom", "/tf", "/tf_static", "/imu/data", "/mission_status/state",
    "/navcam/central/image_color", "/navcam/left/image_color", "/navcam/right/image_color",
    "/navcam/central/camera_info", "/navcam/left/camera_info", "/navcam/right/camera_info",
    "/navcam/central/depth_ground_truth", "/navcam/central/object_mask", "/navcam/central/shadow_label",
    "/navcam/central/object_mask_mapping", "/navcam/central/object_mask_classes",
    "/navcam/central/shadow_label_mapping",
    "/event_camera/image_events", "/spad/image", "/spad/binary", "/lidar/points",
)


class GncError(RuntimeError):
    pass


# Docker runs in WSL on Windows and natively on Linux.
HOST = ["wsl.exe", "-e"] if sys.platform == "win32" else []


def wsl(cmd, check=True, timeout=120):
    """A shell command where Docker runs: WSL on Windows, the host itself on Linux."""
    r = subprocess.run(HOST + ["bash", "-lc", cmd], capture_output=True, text=True, timeout=timeout)
    if check and r.returncode != 0:
        raise GncError(f"Docker host command failed ({r.returncode}): {cmd}\n{r.stderr.strip()[-800:]}")
    return r.stdout


def ros_param_set(container, node, key, value, attempts=3):
    """`ros2 param set` in a container, retried: the ros2 CLI fails now and then on its own
    (e.g. "Failed to close the session")."""
    cmd = (f'docker exec {container} bash -lc "source /opt/ros/jazzy/setup.bash; '
           f'ros2 param set /{node} {key} {value}"')
    for attempt in range(attempts):
        try:
            return wsl(cmd, timeout=90)
        except (GncError, subprocess.TimeoutExpired):
            if attempt == attempts - 1:
                raise
            time.sleep(5.0)


def wsl_path(windows_path):
    if not HOST:
        return os.path.abspath(windows_path)
    return wsl(f"wslpath -u {json.dumps(windows_path.replace(chr(92), '/'))}").strip()


class Gnc:
    def __init__(self, cfg, log=print):
        self.api, self.log = cfg["api"].rstrip("/"), log
        self.dashboard, self.planner = cfg["dashboard_container"], cfg["planner_container"]
        self.planner_node = cfg["planner_node"]
        self.avoidance = cfg.get("obstacle_avoidance", True)
        self.node_params = cfg.get("node_params") or {}
        self.topics = TOPICS + tuple(cfg.get("extra_topics") or ())   # e.g. the rear camera

    # --- API --------------------------------------------------------------------
    def request(self, path, body=None, timeout=30, attempts=5):
        """One API call, retried on transient failures (server errors, timeouts, refused connections)."""
        for attempt in range(attempts):
            req = urllib.request.Request(f"{self.api}/{path}", method="POST" if body is not None else "GET",
                                         data=None if body is None else json.dumps(body).encode(),
                                         headers={"Content-Type": "application/json"})
            try:
                with urllib.request.urlopen(req, timeout=timeout) as r:
                    return json.loads(r.read())
            except urllib.error.HTTPError as error:
                if error.code < 500 or attempt == attempts - 1:
                    raise
            except (urllib.error.URLError, TimeoutError, ConnectionError):
                if attempt == attempts - 1:
                    raise
            time.sleep(1.0 + attempt)

    def status(self):
        return self.request("status")

    def require_running(self):
        nodes = self.status().get("nodes", {})
        down = [n for n, s in nodes.items() if s != "active"]
        if down:
            raise GncError(f"GNC nodes not active: {down} (start the stack with ./docker_build.sh)")

    def apply_node_params(self):
        """Sets the configured parameters on the GNC nodes and restarts them so they take effect."""
        for node, params in self.node_params.items():
            for key, value in params.items():
                if isinstance(value, (list, tuple)):        # a string array, as ros2 param set reads it
                    value = "[" + ",".join(f"'{v}'" for v in value) + "]"
                ros_param_set(self.dashboard, node, key, value)
            self.request("restart_node", {"name": node})
        t0 = time.monotonic()
        time.sleep(3)
        while time.monotonic() - t0 < 120:
            nodes = self.status().get("nodes", {})
            if all(nodes.get(n) == "active" for n in self.node_params):
                return
            time.sleep(2)
        raise GncError(f"GNC nodes did not come back after setting {list(self.node_params)}")

    # --- planner maps -------------------------------------------------------------
    def set_planner_maps(self, map_name, elevation_csv, soil_csv):
        """Copies a map pair into the planner's container and reloads the planner with it."""
        target = f"/tmp/mare_dataset/{map_name}"
        wsl(f"docker exec {self.planner} mkdir -p {target}")
        paths = {}
        for key, src in (("elevation_map_path", elevation_csv), ("soil_map_path", soil_csv)):
            dst = f"{target}/{src.replace(chr(92), '/').rsplit('/', 1)[-1]}"
            wsl(f"docker cp {json.dumps(wsl_path(src))} {self.planner}:{dst}")
            ros_param_set(self.planner, self.planner_node, key, dst)
            paths[key] = dst
        self.request("restart_node", {"name": self.planner_node})
        t0 = time.monotonic()
        time.sleep(3)
        while time.monotonic() - t0 < 120:
            if self.status().get("nodes", {}).get(self.planner_node) == "active":
                return paths
            time.sleep(2)
        raise GncError("The planner did not come back after loading the new maps")

    def apply_avoidance(self):
        """Sets the planner's lidar obstacle avoidance (re-planning) as configured; confirms it."""
        self.request("avoidance", {"on": bool(self.avoidance)})
        for _ in range(20):
            time.sleep(0.5)
            if self.status().get("obstacle_avoidance") == bool(self.avoidance):
                return
        raise GncError(f"The planner did not switch obstacle avoidance to {self.avoidance}")

    # --- the rover ----------------------------------------------------------------
    def wait_landed(self, timeout_s, settle_s=4.0, min_s=8.0):
        """Waits until odometry arrives and the rover has stopped moving (it lands after Play)."""
        t0, last, still_since = time.monotonic(), None, None
        while time.monotonic() - t0 < timeout_s:
            pose = self.status().get("pose")
            if pose:
                p = (pose["x"], pose["y"], pose["z"])
                if last and math.dist(p, last) < 0.01:
                    still_since = still_since or time.monotonic()
                    if time.monotonic() - still_since >= settle_s and time.monotonic() - t0 >= min_s:
                        return pose
                else:
                    still_since = None
                last = p
            time.sleep(0.5)
        raise GncError("The rover did not settle after Play")

    def drive_to(self, x, y, timeout_s):
        """Sends a goal and waits for the mission to end. Returns (state, reason, seconds)."""
        self.request("goal", {"x": float(x), "y": float(y)})
        t0 = time.monotonic()
        time.sleep(2.0)
        while time.monotonic() - t0 < timeout_s:
            mission = self.status().get("mission") or {}
            if mission.get("state") in FINAL_STATES:
                return mission["state"], mission.get("reason", ""), round(time.monotonic() - t0, 1)
            time.sleep(1.0)
        self.request("stop", {})
        return "timeout", f"no result in {timeout_s} s", round(time.monotonic() - t0, 1)

    # --- recording ------------------------------------------------------------------
    def record_start(self, name, max_s):
        """Starts `ros2 bag record` in the dashboard container, stopped after max_s at the latest."""
        bag = f"/tmp/mare_dataset_bags/{name}"
        wsl(f"docker exec {self.dashboard} bash -lc 'mkdir -p /tmp/mare_dataset_bags; rm -rf {bag}'")
        cmd = (f'docker exec {self.dashboard} bash -lc "source /opt/ros/jazzy/setup.bash; '
               f'timeout -s INT {int(max_s)} ros2 bag record -s mcap --storage-preset-profile zstd_fast '
               f'-o {bag} {" ".join(self.topics)}"')
        proc = subprocess.Popen(HOST + ["bash", "-lc", cmd],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(3.0)
        return proc, bag

    def record_stop(self, proc):
        wsl(f"docker exec {self.dashboard} pkill -INT -f 'ros2 bag record'", check=False)
        try:
            proc.wait(timeout=60)
        except subprocess.TimeoutExpired:
            proc.kill()

    def fetch_bag(self, bag, dest_dir):
        """
        Copies a recorded bag out of the container into dest_dir, file by file through a
        pipe (on Windows, to Windows from WSL: a `docker cp` onto /mnt/c goes through WSL's page
        cache and fails with "cannot allocate memory" on bags of several GB).
        """
        out = os.path.join(dest_dir, bag.rstrip("/").rsplit("/", 1)[-1])
        if os.path.isdir(out):
            shutil.rmtree(out)                          # a half-copied bag from an interrupted run
        os.makedirs(out)
        for name in wsl(f"docker exec {self.dashboard} ls {bag}").split():
            part = os.path.join(out, name + ".part")
            with open(part, "wb") as f:
                r = subprocess.run(HOST + ["docker", "exec", self.dashboard, "cat", f"{bag}/{name}"],
                                   stdout=f, stderr=subprocess.PIPE, timeout=1800)
            if r.returncode != 0:
                raise GncError(f"Copying {bag}/{name} failed: {r.stderr.decode(errors='replace')[-400:]}")
            size = int(wsl(f"docker exec {self.dashboard} stat -c %s {bag}/{name}").strip())
            if os.path.getsize(part) != size:
                raise GncError(f"Copying {bag}/{name}: {os.path.getsize(part)} of {size} bytes")
            os.replace(part, os.path.join(out, name))
        wsl(f"docker exec {self.dashboard} rm -rf {bag}", check=False)
