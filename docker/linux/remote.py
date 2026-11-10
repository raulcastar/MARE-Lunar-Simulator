"""
`mare.sh play|stop|status`: starts or stops Play, or reports the open level and
whether Play runs, in a running MARE editor (windowed or headless), over Python
Remote Execution.

Uses Terrain Studio's Remote Execution client (its unreal_link folder, next to
this repository), which also discovers the editor over loopback unicast: on
Linux the editor's multicast socket, bound to 127.0.0.1, hears no multicast.
Runs on the engine's own Python in the mare/unreal image; standard library only.
"""
import json
import os
import sys
import time

STUDIO_LINK = os.path.join(os.environ.get("MARE_SUITE", ""), "Unreal-Moon-Terrain-Generation",
                           "Terrain_Studio", "unreal_link")
STATE = "MARE_STATE "
QUERY = ("import json, unreal\n"
         "les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)\n"
         "ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)\n"
         "w = ues.get_editor_world() or ues.get_game_world()    # during Play: the game world\n"
         "level = w.get_outermost().get_path_name().replace('UEDPIE_0_', '') if w else None\n"
         f"print({STATE!r} + json.dumps({{'level': level, 'playing': bool(les.is_in_play_in_editor())}}))\n")


def fail(message):
    print(message, file=sys.stderr)
    sys.exit(1)


def main(action):
    if not os.path.isfile(os.path.join(STUDIO_LINK, "remote_execution.py")):
        fail(f"Terrain Studio's Remote Execution client is missing ({STUDIO_LINK}): "
             "clone Unreal-Moon-Terrain-Generation next to this repository.")
    sys.path.insert(0, STUDIO_LINK)
    import remote_execution

    session = remote_execution.RemoteExecution()
    session.start()
    try:
        deadline = time.monotonic() + 10.0
        while not session.remote_nodes:
            if time.monotonic() > deadline:
                fail("No Unreal Editor answered: is it running (mare.sh editor or headless)?")
            time.sleep(0.1)
        session.open_command_connection(session.remote_nodes[0]["node_id"])

        def run(code):
            result = session.run_command(code)
            lines = [str(e.get("output", "")) for e in result.get("output", [])]
            if not result.get("success"):
                fail("Failed in Unreal: " + str(result.get("result", "")) + "\n" + "\n".join(lines))
            return lines

        def state():
            for line in run(QUERY):
                if line.startswith(STATE):
                    return json.loads(line[len(STATE):])
            fail("No answer from Unreal")

        if action in ("play", "stop"):
            begin = action == "play"
            if state()["playing"] != begin:
                call = "editor_request_begin_play" if begin else "editor_request_end_play"
                run(f"import unreal\nunreal.get_editor_subsystem(unreal.LevelEditorSubsystem).{call}()\n")
                for _ in range(60):
                    time.sleep(0.5)
                    if state()["playing"] == begin:
                        break
                else:
                    fail("Play did not " + ("start" if begin else "stop"))
        s = state()
        print(f"level: {s['level']}\nplaying: {'yes' if s['playing'] else 'no'}")
    finally:
        session.stop()


if __name__ == "__main__":
    if len(sys.argv) != 2 or sys.argv[1] not in ("play", "stop", "status"):
        fail("usage: remote.py play|stop|status")
    main(sys.argv[1])
