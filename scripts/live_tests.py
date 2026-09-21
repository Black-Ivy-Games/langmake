"""Exercise the real window, GPU capture, and live authoring contract on Windows."""
import argparse
import concurrent.futures
import json
import os
import pathlib
import shutil
import subprocess
import time
import uuid

ROOT = pathlib.Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", required=True)
    args = parser.parse_args()
    engine = pathlib.Path(args.engine).resolve()
    work = ROOT / ".cache" / "live-tests" / uuid.uuid4().hex
    project = work / "courtyard"
    shutil.copytree(ROOT / "examples" / "3d", project, ignore=shutil.ignore_patterns(".langmake"))
    checks = []

    def run(*arguments, ok=True):
        result = subprocess.run([str(engine), *map(str, arguments)], cwd=work,
                                capture_output=True, text=True, timeout=20)
        value = json.loads(result.stdout)
        assert (result.returncode == 0) == ok, (arguments, result.returncode, value, result.stderr)
        assert value.get("ok", True) == ok, value
        return value

    def control(action, *arguments, ok=True):
        return run("control", "--project", project, "--action", action, "--include-state", *arguments, ok=ok)

    def live():
        return run("inspect", "--project", project, "--live", "--check-source")

    def until(predicate, description):
        end = time.monotonic() + 8
        while time.monotonic() < end:
            try:
                state = live()
                if predicate(state):
                    checks.append(description)
                    return state
            except (AssertionError, FileNotFoundError, json.JSONDecodeError):
                pass
            time.sleep(.1)
        raise AssertionError("Timed out: " + description)

    with (work / "preview.stdout").open("w") as stdout, (work / "preview.stderr").open("w") as stderr:
        process = subprocess.Popen([str(engine), "preview", "--project", str(project), "--no-audio"],
                                   cwd=work, stdout=stdout, stderr=stderr)
        try:
            initial = until(lambda state: state["fresh"] and state["renderer"] == "Direct3D 11", "preview becomes observable")
            control("pause")
            before = control("snapshot")["state"]
            time.sleep(.15)
            assert control("snapshot")["state"]["tick"] == before["tick"]
            checks.append("pause holds the physics tick")
            input_file = work / "move.json"
            input_file.write_text(json.dumps({"move_x": 1, "move_z": 0, "jump": False}))
            stepped = control("step", "--steps", 30, "--input", input_file)["state"]
            assert stepped["tick"] == before["tick"] + 30
            def player(state):
                return next(e for e in state["entities"] if e["id"] == "player")
            assert player(stepped)["position"][0] > player(before)["position"][0] + 1
            checks.append("live input advances exactly 30 ticks and moves player")
            capture = control("capture")
            image = pathlib.Path(capture["image"])
            assert image.read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
            metadata = json.loads(pathlib.Path(str(image) + ".json").read_text())
            assert metadata["tick"] == stepped["tick"]
            assert metadata["source_revision"] == initial["source_revision"]
            checks.append("GPU capture has correlated state and revision")
            with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
                captures = list(pool.map(lambda _: control("capture"), range(3)))
            assert len({item["image"] for item in captures}) == 3
            checks.append("concurrent capture requests each receive their own image")
            current = control("snapshot")["state"]
            request_base = time.time_ns()
            request_ids = [str(request_base), str(request_base + 1)]
            for request_id, action in zip(request_ids, ["capture", "step"]):
                request = {"id": request_id, "session": current["session"], "action": action, "steps": 9, "include_state": True}
                target = project / ".langmake" / "commands" / (request_id + ".json")
                temporary = target.with_suffix(".tmp")
                temporary.write_text(json.dumps(request))
                os.replace(temporary, target)
            responses = []
            for request_id in request_ids:
                target = project / ".langmake" / "results" / (request_id + ".json")
                deadline = time.monotonic() + 8
                while not target.exists() and time.monotonic() < deadline:
                    time.sleep(.02)
                responses.append(json.loads(target.read_text()))
            captured = json.loads(pathlib.Path(responses[0]["image"] + ".json").read_text())
            assert captured["tick"] == current["tick"]
            assert responses[1]["state"]["tick"] == current["tick"] + 9
            assert len(captured["view_matrix"]) == 16 and len(captured["camera_position"]) == 3
            checks.append("capture state precedes later queued steps and records rendered camera")
            duplicate = run("preview", "--project", project, "--frames", 1, "--no-audio", ok=False)
            assert "lock" in str(duplicate).lower()
            checks.append("second preview cannot acquire project lock")
            revision = run("inspect", "--project", project)["revision"]
            patch_file = work / "patch.json"
            patch_file.write_text(json.dumps({"expected_revision": revision, "changes": [
                {"id": "player", "property": "color", "value": [1, .2, .7, 1]}]}))
            run("patch", "--project", project, "--file", patch_file)
            changed = until(lambda state: state["source_revision"] != revision and state["matches_source"], "valid patch reloads live scene")
            assert all(abs(a - b) < 1e-5 for a, b in zip(player(changed)["color"], [1, .2, .7, 1]))
            good_manifest = (project / "project.json").read_bytes()
            (project / "project.json").write_text("{ broken JSON")
            broken = until(lambda state: not state["source_valid"] and "INVALID SOURCE" in state["reload_status"], "invalid manifest retains last good scene with diagnostics")
            assert broken["source_revision"] == changed["source_revision"]
            control("snapshot")
            control("capture")
            control("reset", ok=False)
            checks.append("live observation and controls work while authored manifest is invalid")
            (project / "project.json").write_bytes(good_manifest)
            until(lambda state: state["source_valid"] and not state["diagnostic"], "restoring identical valid source clears reload diagnostic")
            script = project / "scripts" / "game.luau"
            good_script = script.read_bytes()
            script.write_text("return { update = function( invalid syntax }")
            rejected = until(lambda state: "RELOAD FAILED" in state["reload_status"], "invalid Luau edit retains live scene")
            assert rejected["source_revision"] == changed["source_revision"]
            script.write_bytes(good_script)
            until(lambda state: state["matches_source"] and not state["diagnostic"], "script recovery clears diagnostic")
            control("reset")
            reset = control("snapshot")["state"]
            assert reset["tick"] == 0 and reset["collected"] == 0
            control("resume")
            until(lambda state: not state["paused"] and state["tick"] > 1, "resume advances simulation again")
            script.write_text("return {update=function() error('live failure regression') end}")
            until(lambda state: state["paused"] and "live failure regression" in state["diagnostic"], "runtime script failure pauses and remains observable")
            control("step", "--steps", 1, ok=False)
            assert "live failure regression" in control("snapshot")["state"]["diagnostic"]
            failing_project = work / "failing-capture"
            shutil.copytree(project, failing_project, ignore=shutil.ignore_patterns(".langmake"))
            failed_capture = run("capture", "--project", failing_project, "--frames", 4,
                                 "--output", work / "failed-capture.png", ok=False)
            assert failed_capture["diagnostics"][0]["code"] == "SCRIPT_ERROR"
            checks.append("bounded graphics run reports script failure with nonzero exit")
            report = {"ok": True, "checks": checks, "fixture": str(project), "image": str(image)}
            (work / "report.json").write_text(json.dumps(report, indent=2))
            print(json.dumps(report, indent=2))
        finally:
            process.terminate()
            process.wait(timeout=10)


if __name__ == "__main__":
    main()
