"""
Headless check of the project in the Linux editor (mare.sh smoke): the
project's C++ plugins loaded, every level open with its terrain and rover,
then quit. The MuJoCo plugins and level are checked when mare.sh enables them
(MARE_MUJOCO=1). Prints MARE_SMOKE OK or MARE_SMOKE FAIL lines to the log.
"""
import os

import unreal

failures = []
MUJOCO = os.environ.get("MARE_MUJOCO") == "1"

# A class from each project plugin, by its /Script path: found only if its
# module built and loaded. (unreal.<Name> would miss classes not exposed to
# Blueprint, such as SimClockSubsystem.)
PLUGIN_CLASSES = ["/Script/UnrealDEMLandscape.DEMLandscapeLibrary",
                  "/Script/UnrealStereoCam.UnrealStereoCam",
                  "/Script/UnrealLidarCam.UnrealLidarCam",
                  "/Script/UnrealEventCam.UnrealEventCam",
                  "/Script/UnrealSPADCam.UnrealSPADCam",
                  "/Script/UnrealSimClock.SimClockSubsystem",
                  "/Script/TCPWrapper.TCPClientComponent"]
LEVELS = ["/Game/Lunar_Terrain_Raph", "/Game/Lunar_Terrain_Yutu"]
if MUJOCO:
    PLUGIN_CLASSES += ["/Script/URLab.AMjManager",
                       "/Script/URLab.MjArticulation",
                       "/Script/MAREMuJoCoBridge.MareMuJoCoRoverBridge",
                       "/Script/MAREMuJoCoBridge.MareRollingTerrain"]
    LEVELS += ["/Game/Lunar_Terrain_Raph_MuJoCo"]


def check(ok, what):
    unreal.log(f"MARE_SMOKE {'OK' if ok else 'FAIL'} {what}")
    if not ok:
        failures.append(what)


for path in PLUGIN_CLASSES:
    check(unreal.load_class(None, path) is not None, f"plugin class {path}")

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
unreal.log(f"MARE_SMOKE MuJoCo {'enabled' if MUJOCO else 'disabled: skipping its plugins and level'}")
for level in LEVELS:
    check(levels.load_level(level), f"load {level}")
    all_actors = actors.get_all_level_actors()
    terrain = [a.get_actor_label() for a in all_actors if isinstance(a, unreal.LandscapeProxy)]
    check(bool(terrain), f"{level}: a Landscape {terrain}")
    labels = [a.get_actor_label() for a in all_actors]
    check(any(("raph" in l.lower()) or ("yutu" in l.lower()) for l in labels), f"{level}: a rover")

unreal.log(f"MARE_SMOKE {'PASSED' if not failures else 'FAILED: ' + '; '.join(failures)}")
# Exit straight away with the result: the editor's own shutdown on Linux
# crashes in SDL teardown (double free) and then hangs. The shader cache lives
# in its own process (zenserver), so nothing is lost.
unreal.SystemLibrary.execute_console_command(None, "FlushLog")
os._exit(1 if failures else 0)
