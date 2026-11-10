"""
Run by `mare.sh headless` once the editor has started: waits for the level to
settle, then starts Play, as pressing Play in the editor would. The editor has
no window (-RenderOffscreen) but renders as usual, so every sensor works.
Prints MARE_HEADLESS lines to the log.
"""
import time

import unreal

SETTLE_S = 10.0     # after the first editor tick: the level's actors and streaming settle
_start = None
_handle = None


def _tick(_delta):
    global _start
    if _start is None:
        _start = time.monotonic()
        unreal.log("MARE_HEADLESS editor ticking; Play in %.0f s" % SETTLE_S)
        return
    if time.monotonic() - _start < SETTLE_S:
        return
    unreal.unregister_slate_post_tick_callback(_handle)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    level = world.get_outermost().get_path_name() if world else None
    if not les.is_in_play_in_editor():
        les.editor_request_begin_play()
    unreal.log(f"MARE_HEADLESS Play requested in {level}")


_handle = unreal.register_slate_post_tick_callback(_tick)
