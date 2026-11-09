"""
Headless check of the project in the Linux editor (mare.sh smoke): the
project's C++ plugins loaded, both levels open with their rover, then quit.
Prints MARE_SMOKE OK or MARE_SMOKE FAIL lines to the log.
"""
import unreal

failures = []


def check(ok, what):
    unreal.log(f"MARE_SMOKE {'OK' if ok else 'FAIL'} {what}")
    if not ok:
        failures.append(what)


# A class from each project plugin: present only if its module built and loaded.
for name in ("DEMLandscapeLibrary", "UnrealStereoCam", "UnrealLidarCam", "UnrealEventCam",
             "UnrealSPADCam", "SimClockSubsystem", "TCPClientComponent"):
    check(hasattr(unreal, name), f"plugin class unreal.{name}")

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for level in ("/Game/Lunar_Terrain_Raph", "/Game/Lunar_Terrain_Yutu"):
    check(levels.load_level(level), f"load {level}")
    labels = [a.get_actor_label() for a in actors.get_all_level_actors()]
    check(any("landscape" in l.lower() for l in labels), f"{level}: a Landscape")
    check(any(("raph" in l.lower()) or ("yutu" in l.lower()) for l in labels), f"{level}: a rover")

unreal.log(f"MARE_SMOKE {'PASSED' if not failures else 'FAILED: ' + '; '.join(failures)}")
unreal.SystemLibrary.quit_editor()
