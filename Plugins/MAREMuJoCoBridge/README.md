# MARE MuJoCo Bridge

Optional MuJoCo physics for the Raph Rover, using [MuJoCo](https://github.com/google-deepmind/mujoco) through
[Unreal Robotics Lab (URLab)](https://github.com/URLab-Sim/UnrealRoboticsLab). The rover is driven by the same
GNC stack as the Chaos rover; the Chaos levels are unchanged.

| Level | Physics |
|---|---|
| `Lunar_Terrain_Raph`, `Lunar_Terrain_Yutu` | Chaos |
| `Lunar_Terrain_Raph_MuJoCo` | MuJoCo |

## Usage

1. Start the GNC stack and select **Raph** in the dashboard's ROVER menu.
2. Open `Lunar_Terrain_Raph_MuJoCo` and press Play.

## Contents

- **MARE MuJoCo Rover Bridge** (`AMareMuJoCoRoverBridge`): connects the rover to the GNC rover node using the
  Chaos rover Blueprint's protocol (TCP 3003). Wheel inputs (FL/BL/FR/BR) drive the MuJoCo wheel actuators;
  optional steering angles (SFL/SBL/SFR/SBR, rad, positive = left) drive the steering actuators, and a
  missing angle holds that wheel straight. `base_link` telemetry is returned with UnrealSimClock
  timestamps. It attaches the sensor rig, spins and steers the rig's wheel meshes, drives its headlights,
  and steps MuJoCo with the game clock.
- **MARE Rolling Terrain** (`AMareRollingTerrain`): keeps a URLab heightfield centred on the rover,
  re-sampling only the uncovered strip when the rover moves `RecentreDistance` from the centre.

Related project files:

- `Content/MuJoCo/RaphRover/`: URLab articulation (`raph_rover`), sensor rig (`Leo_Raph_MuJoCo_Sensors`,
  a copy of the Leo_Raph Blueprint with physics and TCP disabled), imported meshes and materials.
- `MuJoCo/raph_rover.xml`: the rover's MJCF model (collision geometry), loadable in MuJoCo on its own.

## Model

- **Rover**: 11.6 kg, box chassis, four cylinder wheels driven by velocity servos (kv 20, ±8 N·m). Wheel
  armature (0.2 kg·m², gearmotor inertia) gives skid steering close to the Chaos rover (effective track
  ~1.0 m vs 0.89 m); wheel friction (1 N·m) holds the rover on slopes.
- **Steering**: each wheel hangs from a steering body (`steer_*_link`) on a vertical hinge (±90°) driven by
  a stiff position servo (kp 3000, ±60 N·m), so it holds within 0.2° of the commanded angle under skid
  loads. The GNC stack sends no steering yet, so the wheels stay straight and the rover skid-steers as
  before. The wheels are excluded from contact with the chassis box, which a steered wheel would cross.
- **Sensors**: the sensor rig is attached to the MuJoCo `base_link`, so all sensors keep the poses in
  `leo_raph.urdf`. Its meshes are rendered, with wheels driven by the MuJoCo joints. It is labelled
  `Leo_Raph` for Terrain Studio's wheel tracks.
- **Time**: URLab's loop stays paused and the bridge advances MuJoCo by each frame's game time, so physics,
  cameras and telemetry share Unreal's clock. Pressing P returns stepping to URLab.
- **Terrain**: a 40 m heightfield (512², 7.8 cm) sampled from the landscape and rocks, recentred on the
  rover every 8 m.

## Setup

URLab is a git submodule (`Plugins/UnrealRoboticsLab`); clone with `--recursive`, or run
`git submodule update --init Plugins/UnrealRoboticsLab`. Its native dependencies (MuJoCo, CoACD, libzmq)
are built once per machine.

**Windows:**

```powershell
cd Plugins/UnrealRoboticsLab/third_party
.\build_all.ps1            # requires CMake 3.24+ and Visual Studio 2022
```

Then build the project (`Build.bat Moon_SimulatorEditor Win64 Development -Project=...`) or let the editor
compile it. Requires the UnrealRoboticsLab and UnrealSimClock plugins.

**Linux (Docker):** `docker/linux/mare.sh build` does it all: it fetches the submodule if needed, builds
the dependencies the first time with the engine's own clang, compiles the project, and links the
libraries next to the plugin. Then open the level with `docker/linux/mare.sh editor /Game/Lunar_Terrain_Raph_MuJoCo`
(or `headless`, which also starts Play).

## Notes

- `r.Shadow.Virtual.MaxPhysicalPages=8192` (Config/DefaultEngine.ini) prevents black shadow tiles caused by
  the rover's scene captures exhausting the virtual shadow map pool.
- URLab's Simulate panel is disabled in the level (MuJoCo Manager > Auto Create Simulate Widget).
