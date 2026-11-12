# MARE: Lunar Rover Simulator

[![arXiv](https://img.shields.io/badge/arXiv-2610.28465-b31b1b?logo=arxiv&logoColor=white)](https://arxiv.org/abs/2610.28465)
[![Video](https://img.shields.io/badge/Video-YouTube-FF0000?logo=youtube&logoColor=white)](https://youtu.be/EI4HgbMFbBE)
[![Project page](https://img.shields.io/badge/Project-page-1f6feb)](https://raulcastar.me/mare/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Unreal Engine 5.7](https://img.shields.io/badge/Unreal%20Engine-5.7-313131?logo=unrealengine&logoColor=white)](https://www.unrealengine.com/)
[![ROS 2 Jazzy](https://img.shields.io/badge/ROS%202-Jazzy-22314E?logo=ros&logoColor=white)](https://docs.ros.org/en/jazzy/)
[![Windows](https://img.shields.io/badge/Windows-10%20%7C%2011-0078D6?logo=windows&logoColor=white)](#requirements)
[![Linux](https://img.shields.io/badge/Linux-Docker-FCC624?logo=linux&logoColor=black)](#linux)

**MARE** (*Modular Autonomy and Rendering Environment*, after the lunar maria)
is an open-source lunar rover simulator built on Unreal Engine 5 and ROS 2. It generates
lunar terrain with craters, rocks and a low sun, renders it for the rover's
cameras and sensors with ground truth, and drives the rover autonomously with a
ROS 2 GNC stack, watched from a web mission control. It runs on Windows and,
with the whole suite in Docker, on Linux.

**Paper:** [arXiv:2610.28465](https://arxiv.org/abs/2610.28465) · **Video overview:** [YouTube](https://youtu.be/EI4HgbMFbBE) · **Project page:** [raulcastar.me/mare](https://raulcastar.me/mare/)

**Dataset:** [MARE Lunar Rover Dataset](https://huggingface.co/datasets/raulcastar/mare-lunar-rover-dataset) (5,040 frames, CC BY 4.0) · **Baselines:** [trained models](https://huggingface.co/raulcastar/mare-lunar-baselines) · [code](https://github.com/raulcastar/MARE-Dataset-Baselines)

| Leo_Raph | Yutu |
|---|---|
| ![Leo_Raph driving itself to a goal past rocks](docs/images/raph_drive.webp) | ![Yutu driving itself to a goal](docs/images/yutu_drive.webp) |

*Both rovers driving themselves to a goal past rocks with the same GNC stack,
seen from Unreal's orbit camera (6x speed). Videos at 2x:
[Leo_Raph](docs/videos/raph_drive.mp4), [Yutu](docs/videos/yutu_drive.mp4).
Leo_Raph is a model of Fictionlab's [Raph Rover](https://fictionlab.pl/raph-rover/); Yutu is adapted from a
public model of the Chang'e 3 rover.*

This repository is the Unreal project. It holds the levels, the rovers and the
sensor plugins, and it is the centre of the suite:

| Repository | Role |
|---|---|
| **MARE-Lunar-Simulator** (this one) | Unreal Engine 5.7 project: levels, rovers (Leo_Raph, Yutu), sensor plugins |
| [Unreal-Moon-Terrain-Generation](https://github.com/raulcastar/Unreal-Moon-Terrain-Generation) | Terrain Studio: terrain, craters, rocks, ground-truth masks and the sun, built in Unreal |
| [GNC-Moon-Simulator](https://github.com/raulcastar/GNC-Moon-Simulator) | ROS 2 GNC stack in Docker: perception, path planning, rover control, mission control |
| [UnrealStereoCam](https://github.com/raulcastar/UnrealStereoCam), [UnrealLidarCam](https://github.com/raulcastar/UnrealLidarCam), [UnrealEventCam](https://github.com/raulcastar/UnrealEventCam), [UnrealSPADCam](https://github.com/raulcastar/UnrealSPADCam), [UnrealSimClock](https://github.com/raulcastar/UnrealSimClock) | The sensor plugins, each usable in any Unreal project (copies in `Plugins/`) |
| [UnrealDEMLandscape](https://github.com/raulcastar/UnrealDEMLandscape) | Editor plugin that creates the Landscape from Terrain Studio's heightmap, usable in any Unreal project (copy in `Plugins/`) |
| [MARE-Dataset-Baselines](https://github.com/raulcastar/MARE-Dataset-Baselines) | Segmentation and depth baselines trained on the [dataset](https://huggingface.co/datasets/raulcastar/mare-lunar-rover-dataset) recorded with MARE; the generator that recorded it is in [`dataset/`](dataset) (optional) |

## The pipeline

![The pipeline: Terrain Studio builds the level in Unreal; the sensor plugins stream to the GNC stack, which drives the rover](docs/images/pipeline.svg)

1. **Terrain Studio** generates a DEM (or loads a real one), plans a rock field around drivable corridors, and builds both in Unreal with the ground-truth masks and the sun.
2. **Unreal** simulates the rover and renders its sensors: stereo navcams with depth and object masks, lidar, event and SPAD cameras, all stamped with the simulation time.
3. **The GNC stack** turns the sensor streams into depth and local maps, plans a route on the same DEM, and drives the rover back through Unreal.

Step by step, with what each link carries: [docs/pipeline.md](docs/pipeline.md).

## What it looks like

**Terrain.** A 300 × 300 m mare from Terrain Studio, with its 974 planned rocks
under a 10° sun, and the studio's predicted shadows next to Unreal's as the sun
sets ([video](docs/videos/flyover.mp4)):

![A fly-over of a generated mare in Unreal](docs/images/flyover.webp)

![Terrain Studio's predicted shadows next to Unreal's, as the sun sets](docs/images/sun_studio_vs_unreal.webp)

**Sensors.** Stereo depth, lidar, event and SPAD cameras on the rover:

![Stereo depth, lidar, event camera and SPAD camera](docs/images/sensors.webp)

**Headlights.** The rovers carry dimmable headlights, switched from mission
control or an AI agent. With the sun almost off, only what the beams reach is
visible, as on a night drive or inside a permanently shadowed crater (left: orbit
camera; right: the front navcam):

![The Raph Rover lighting the dark terrain with its headlights, and its navcam view](docs/images/headlights.webp)

**Ground truth.** The navcams and the rear camera come with pixel-exact masks
of object classes and shadows, including the rover's own shadow and its wheel
tracks:

![Front and rear cameras with their object and shadow masks](docs/images/ground_truth.webp)

*The front navcam and the rear camera during a drive, with their ground-truth object
masks and shadow masks (2.4x speed; [video](docs/videos/ground_truth.mp4) at 1.2x). Ahead, the rover's own
shadow (orange) is labelled apart from the terrain's shadows; behind, the wheel
tracks it leaves are their own class in the object mask.*

**Mission control.** The web dashboard during a drive: navcams with the planned
path, masks, global map, lidar, local elevation map and telemetry:

![The mission control overview during a drive](docs/images/mission_control.webp)

## Requirements

MARE runs on **Windows** and **Linux**.

- **Windows 10/11:** Unreal Engine **5.7** and Visual Studio 2022 (*Game development with C++*) to build the plugins on first open, and Docker on WSL2 for the GNC stack.
- **Linux:** Docker, an NVIDIA GPU with its driver, and the [NVIDIA Container Toolkit](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/install-guide.html). Everything else runs in containers ([Linux](#linux)).
- **Both:** [Git LFS](https://git-lfs.com) (the assets and levels are stored in LFS), a recent GPU (the project uses Lumen and virtual shadow maps), and about 32 GB of RAM to run Unreal and the stack together.

## Quick start

```sh
git lfs install                # once: the Unreal assets are in Git LFS
git clone --recursive https://github.com/raulcastar/MARE-Lunar-Simulator.git   # with the URLab submodule
git clone https://github.com/raulcastar/GNC-Moon-Simulator.git
git clone https://github.com/raulcastar/Unreal-Moon-Terrain-Generation.git
```

Clone them side by side; what each is for, and the plugin repositories:
[docs/setup.md](docs/setup.md#clone-the-suite).

**Windows:**

1. Open `Moon_Simulator.uproject` and let Unreal build the plugins ([docs/setup.md](docs/setup.md)). It opens on `Lunar_Terrain_Raph`.
2. Start the GNC stack ([GNC-Moon-Simulator](https://github.com/raulcastar/GNC-Moon-Simulator#quick-start)), press **Play**, and open mission control at http://127.0.0.1:5000 to send the rover to a goal.
3. To build your own terrain, run [Terrain Studio](https://github.com/raulcastar/Unreal-Moon-Terrain-Generation#quick-start) with the editor open.

**Linux** (after [getting access to Epic's image](#linux), once):

```sh
MARE-Lunar-Simulator/docker/linux/mare.sh build      # the editor image, then the plugins
GNC-Moon-Simulator/docker_build.sh                   # builds and starts the GNC stack
MARE-Lunar-Simulator/docker/linux/mare.sh editor     # the editor on Lunar_Terrain_Raph
MARE-Lunar-Simulator/docker/linux/mare.sh play       # or press Play in the editor
```

Then open mission control at http://127.0.0.1:5000. For your own terrain, run
`Unreal-Moon-Terrain-Generation/run_terrain_studio_docker.sh`.

## Linux

The whole suite runs on Linux in Docker: the Unreal Editor, the GNC stack,
Terrain Studio and the dataset generator. It is tested on Ubuntu and Arch Linux
with NVIDIA RTX GPUs.

The editor is built on Epic's Unreal Engine image on ghcr.io (the GitHub
Container Registry), which Epic shares only with accounts that have accepted
the Unreal Engine EULA. To get access, once:

1. Link your GitHub account to your Epic Games account ([Epic's guide](https://www.unrealengine.com/en-US/ue-on-github)).
2. Accept the invitation to the EpicGames organisation on GitHub, sent by email after linking.
3. Create a classic GitHub token with the `read:packages` scope.
4. Sign in with `docker login ghcr.io -u <github user>`, using the token as the password.

Commands, troubleshooting and what differs from Windows:
[docs/setup.md](docs/setup.md#linux-in-docker).

## MuJoCo physics

`Lunar_Terrain_Raph_MuJoCo` runs the Raph Rover on [MuJoCo](https://github.com/google-deepmind/mujoco)
through the [Unreal Robotics Lab](https://github.com/URLab-Sim/UnrealRoboticsLab) plugin (URLab),
driven by the same GNC stack. The other levels keep Unreal's Chaos physics.

URLab is a git submodule in `Plugins/UnrealRoboticsLab`, pinned to a tested
commit. Clone with `--recursive`, or fetch it in an existing clone:

```sh
git submodule update --init
```

The project needs URLab built to open, whichever level you use. Its native
libraries (MuJoCo, CoACD, libzmq) are built once per machine:

- **Linux:** `docker/linux/mare.sh build` fetches the submodule and builds everything.
- **Windows:** run `Plugins/UnrealRoboticsLab/third_party/build_all.ps1` (CMake 3.24+ and Visual Studio 2022), then open the project.

Usage and the rover model: [Plugins/MAREMuJoCoBridge](Plugins/MAREMuJoCoBridge/README.md).

## AI agents

Two MCP servers let an AI agent (Claude Code, Claude Desktop or any MCP client)
run the suite end to end: Terrain Studio's builds terrains, rocks and the sun
in Unreal and starts Play; the GNC stack's sends goals, drives, reads the
cameras and records bags. See each repository's README.

## Documentation

| | |
|---|---|
| [Pipeline](docs/pipeline.md) | The full workflow, from a terrain to an autonomous drive, and every link between the parts |
| [Setup](docs/setup.md) | Cloning with LFS, building the plugins, connecting the other repositories, [Linux in Docker](docs/setup.md#linux-in-docker) |
| [The Unreal project](docs/project.md) | Levels, rovers, plugins, content layout, project settings, editor Python |
| [Rovers](https://github.com/raulcastar/GNC-Moon-Simulator/blob/main/docs/rovers.md) | Leo_Raph and Yutu, switching rovers, physics tuning (GNC docs) |

## Roadmap

- [x] Linux support (Ubuntu, Arch), alongside Windows: the whole suite in Docker ([Linux](#linux))
- [x] MuJoCo physics for the Raph Rover, through the [Unreal Robotics Lab](https://github.com/URLab-Sim/UnrealRoboticsLab) plugin ([MuJoCo physics](#mujoco-physics))
- [ ] More rover models
- [ ] Lunar lava tubes
- [ ] Wheel steering, including Ackermann steering
- [ ] Image defects caused by cosmic rays, simulated with ESA's [Pyxel](https://sci.esa.int/web/future-missions-department/-/60390-pyxel-a-software-framework-for-imaging-detector-simulation) detector framework

## Citation

If you use MARE in your research, please cite the [paper](https://arxiv.org/abs/2610.28465):

```bibtex
@misc{castilla2026mare,
  title         = {{MARE}: An Open-Source Lunar Rover Simulator with Shadow Ground Truth for Perception and Autonomy},
  author        = {Castilla-Arquillo, Ra{\'u}l and Olivares-Mendez, Miguel},
  year          = {2026},
  eprint        = {2610.28465},
  archivePrefix = {arXiv},
  primaryClass  = {cs.RO},
  url           = {https://arxiv.org/abs/2610.28465}
}
```

## License

[MIT](LICENSE), © 2026 Raul Castilla Arquillo, except the third-party content
listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md): the Poly Haven
landscape textures and rocks (CC0), the Yutu model (CC-BY-4.0), TCP-Unreal
(MIT), the ESIM event model (MIT) and Unreal Engine (Epic's EULA).
