# Setup

## Clone the suite

Clone the repositories side by side in one folder. Terrain Studio finds the GNC
stack's copy of the mask classes when the two sit next to each other.

```sh
git lfs install                # once: this project's levels and assets are in Git LFS
mkdir MARE && cd MARE
git clone https://github.com/raulcastar/MARE-Lunar-Simulator.git
git clone https://github.com/raulcastar/GNC-Moon-Simulator.git
git clone https://github.com/raulcastar/Unreal-Moon-Terrain-Generation.git
```

| Repository | Needed for |
|---|---|
| MARE-Lunar-Simulator | Always: the Unreal project |
| GNC-Moon-Simulator | Driving the rover: perception, planning, mission control |
| Unreal-Moon-Terrain-Generation | Building your own terrains, rocks, masks and sun |

Without Git LFS installed first, the `.uasset` and `.umap` files arrive as small
text pointers that Unreal cannot open; in such a clone, run `git lfs pull`.

The sensor plugins are already in `Plugins/`. Clone their own repositories only
to work on them (then copy the plugin folder back into `Plugins/`):

```sh
git clone https://github.com/raulcastar/UnrealStereoCam.git
git clone https://github.com/raulcastar/UnrealLidarCam.git
git clone https://github.com/raulcastar/UnrealEventCam.git
git clone https://github.com/raulcastar/UnrealSPADCam.git
git clone https://github.com/raulcastar/UnrealSimClock.git
```

## Unreal Engine and the plugins

The project targets **Unreal Engine 5.7** on Windows. The sensor plugins and
TCP-Unreal are in `Plugins/` as source; their binaries are not in the
repository.

- **Build on open (default):** install Visual Studio 2022 with the *Game development with C++* workload, open `Moon_Simulator.uproject`, and answer **Yes** when Unreal offers to rebuild the missing modules.
- **Prebuilt:** each sensor plugin's repository has a release for UE 5.7 Win64. Extract it over the plugin's folder in `Plugins/` (this replaces the source copy with the same version).

Engine plugins the project needs are enabled in the `.uproject` and the
plugins' own descriptors: Python Editor Script Plugin, Editor Scripting
Utilities, Modeling Tools, Json Blueprint Utilities, Procedural Mesh and
Geometry Scripting.

## Linux, in Docker

On Linux the whole suite runs in containers; the host needs only Docker, the
NVIDIA driver and the NVIDIA Container Toolkit. `docker/linux/` builds the
editor from Epic's official image, `ghcr.io/epicgames/unreal-engine:dev-slim-5.7`
(tens of GB). Pulling it needs:

1. Your GitHub account linked to your Epic Games account, and the invitation to
   the EpicGames organisation accepted ([how](https://www.unrealengine.com/en-US/ue-on-github)).
2. `docker login ghcr.io -u <github user>`, with a GitHub token that has
   `read:packages`.

Then, with the suite cloned side by side (the folder holding this repository is
mounted into the containers at its own path):

```sh
docker/linux/mare.sh build     # the image, then the project's plugins for Linux
docker/linux/mare.sh smoke     # headless: plugins loaded, both levels open
docker/linux/mare.sh editor    # the editor on your X server (Wayland: XWayland)
```

The editor uses Vulkan (SM6). Its first start compiles every shader, which takes
long; the caches are kept in the `mare-unreal-ddc` volume. Host networking lets
the sensor plugins reach the GNC containers on `127.0.0.1`, as on Windows, and
Terrain Studio (`run_terrain_studio_docker.sh` in its repository) reach the
editor through Remote Execution. The dataset generator runs with
`dataset/run_docker.sh`. `docker/linux/progress.sh` shows a running editor's
shader compilation, GPU and log.

What differs from Windows, and is handled for you:

- **Remote Execution.** Linux delivers multicast only to sockets bound to
  `0.0.0.0`, never to one bound to `127.0.0.1` as the editor's is, so the
  editor would not hear the discovery pings. Terrain Studio's client (its
  `unreal_link/remote_execution.py`, which the dataset generator uses too)
  therefore also sends them unicast to `127.0.0.1:6766` on Linux. Everything
  stays on loopback: no route or firewall change, and the editor keeps the
  project's settings.
- **Vulkan bindless descriptors.** `mare.sh` raises the editor's heap of
  typed-buffer views (`r.Vulkan.Bindless.MaxResourceUniformTexelBufferCount`)
  from 65,536 to 1,048,576, near what D3D12 has. The levels' wheel-track
  decals alone hold about 16,000, and re-creating them in one frame needs as
  many again before the old ones are freed; a full heap crashes the editor
  ("Attemping to update invalid descriptor handle").
- **Closing the editor** can crash in SDL teardown and hang; stop the container
  (`docker kill`) if it does. `mare.sh smoke` exits on its own.

## UnrealDEMLandscape

Unreal's Python API cannot create a Landscape, so Terrain Studio builds it
through `Plugins/UnrealDEMLandscape`, a small editor plugin
([UnrealDEMLandscape](https://github.com/raulcastar/UnrealDEMLandscape),
`unreal.DEMLandscapeLibrary`). It creates the Landscape with its .r16 heightmap
the way the editor's New Landscape tool does, and reads the heights back to
check the import. It is built with the other plugins and needs no setup.

Without it the project still opens and simulates; only Terrain Studio's
Landscape creation needs it.

## Project settings worth checking

They are set in `Config/DefaultEngine.ini`; check them after cloning:

- **Python → Remote Execution** on, multicast group `239.0.0.1:6766` bound to `127.0.0.1`: Terrain Studio and the GNC tools reach the editor this way.
- **Python → Startup Scripts**: `rover_cameras_startup.py` (the viewport camera follows mission control). `Content/Python/init_unreal.py` runs by itself and starts the Terrain Studio watcher.

## The rest of the suite

| | Where | Needs |
|---|---|---|
| GNC stack | [GNC-Moon-Simulator](https://github.com/raulcastar/GNC-Moon-Simulator) | Docker (on Windows: WSL2 with mirrored networking); ROS 2 Jazzy runs in the containers |
| Terrain Studio | [Unreal-Moon-Terrain-Generation](https://github.com/raulcastar/Unreal-Moon-Terrain-Generation) | Python 3 with Tkinter, or Docker on Linux; the editor open |

They connect to the editor over `127.0.0.1`, so this project does not depend
on where they are cloned ([Clone the suite](#clone-the-suite) puts them side by
side).

## First run

1. Open `Moon_Simulator.uproject`; it opens on `Lunar_Terrain_Raph`.
2. Start the GNC stack (`./docker_build.sh` in GNC-Moon-Simulator).
3. Press **Play**. Within a few seconds the dashboard at http://127.0.0.1:5000 shows the navcams and the rover's pose.
4. Click on the overview map to send a goal.

If a feed stays dark, the [links and ports](https://github.com/raulcastar/GNC-Moon-Simulator/blob/main/docs/unreal.md#links-and-ports)
section of the GNC docs lists what each one needs.
