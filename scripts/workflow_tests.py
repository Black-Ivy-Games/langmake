"""Native agent workflow regressions; temporary windows, never computer-use gameplay.

Fixtures, captures and logs are retained within .cache/workflow-tests. Windows
file-sharing tests deliberately hold replace-denying handles on fixture files.
"""
import argparse
import array
import concurrent.futures
import contextlib
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid
import wave

ROOT = Path(__file__).resolve().parent.parent
sys.dont_write_bytecode = True
from asset_tests import read_pixels


def require(value, message):
    if not value:
        raise AssertionError(message)


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    temporary.write_text(json.dumps(value), encoding="utf-8")
    os.replace(temporary, path)


@contextlib.contextmanager
def deny_replacement(path):
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                  wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    # Permit readers and writers, intentionally omit FILE_SHARE_DELETE.
    handle = kernel.CreateFileW(str(path), 0x80000000, 3, None, 3, 0x80, None)
    if handle == wintypes.HANDLE(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        yield
    finally:
        require(kernel.CloseHandle(handle), "Failed to close fixture lock")


class Workflow:
    def __init__(self, engine, iterations):
        self.engine = engine.resolve()
        self.iterations = iterations
        self.directory = ROOT / ".cache" / "workflow-tests" / uuid.uuid4().hex
        self.directory.mkdir(parents=True)
        self.checks = []

    def run(self, *arguments, ok=True, timeout=45):
        process = subprocess.run([str(self.engine), *map(str, arguments)], cwd=self.directory,
                                 capture_output=True, text=True, timeout=timeout)
        try:
            result = json.loads(process.stdout)
        except json.JSONDecodeError as error:
            raise AssertionError((arguments, process.returncode, process.stdout, process.stderr)) from error
        require((process.returncode == 0) == ok and result.get("ok", True) == ok,
                str((arguments, process.returncode, result, process.stderr)))
        return result

    def passed(self, description):
        self.checks.append(description)
        print("PASS " + description, flush=True)

    def fixture(self, name, dimension="2d", count=2, ui=False):
        directory = self.directory / name
        script = "return { update=function(dt) engine.move_player(engine.input('move_x')*4,engine.input('move_z')*4) end }"
        manifest = {"schema_version": 1, "dimension": dimension, "title": "Workflow " + name,
                    "gravity": [0, 0, 0], "camera": {"position": [8, 8, 18], "target": [0, 0, 0]},
                    "script": "scripts/game.luau", "entities": [
                        {"id": "player", "position": [0, 0, 0], "size": [1, 1, 1], "body": "dynamic", "role": "player"},
                        {"id": "marker", "position": [4, 0, 0], "size": [1, 1, 1], "body": "none", "color": [1, .3, .4, 1]}]}
        for index in range(count - 2):
            manifest["entities"].append({"id": "pooled_%04d" % index, "position": [100, 100, 100],
                                         "size": [.2, .2, .2], "body": "none", "visible": False})
        if ui:
            manifest["ui"] = {"documents": {"hud": "ui/hud.rml"}, "styles": {"main": "ui/main.rcss"}, "data": {"score": 7}}
            (directory / "ui").mkdir(parents=True)
            (directory / "ui/hud.rml").write_text("<rml><head><link type='text/rcss' href='main.rcss'/></head><body><div id='panel'><span id='score'>7</span></div></body></rml>", encoding="utf-8")
            (directory / "ui/main.rcss").write_text("body {width:100%;height:100%;} #panel {position:absolute;right:20px;top:20px;width:180px;height:60px;background-color:#ff2244;color:#ffffff;font-size:30px;} #score {margin:8px;}", encoding="utf-8")
        write_json(directory / "project.json", manifest)
        (directory / "scripts").mkdir()
        (directory / "scripts/game.luau").write_text(script, encoding="utf-8")
        self.run("validate", "--project", directory)
        return directory

    def until(self, operation, predicate, timeout=12):
        deadline = time.monotonic() + timeout
        last = None
        while time.monotonic() < deadline:
            try:
                last = operation()
                if predicate(last):
                    return last
            except (AssertionError, FileNotFoundError, json.JSONDecodeError) as error:
                last = repr(error)
            time.sleep(.05)
        raise AssertionError("Wait expired: " + str(last))

    def live(self, project, *arguments):
        return self.run("inspect", "--project", project, "--live", *arguments)

    def control(self, project, action, *arguments, ok=True):
        return self.run("control", "--project", project, "--action", action, *arguments, ok=ok)

    @contextlib.contextmanager
    def window(self, project, mode="preview"):
        with (project / (mode + ".stdout")).open("w") as out, (project / (mode + ".stderr")).open("w") as err:
            process = subprocess.Popen([str(self.engine), mode, "--project", str(project), "--no-audio", "--no-overlay"],
                                       cwd=self.directory, stdout=out, stderr=err)
            try:
                self.until(lambda: self.live(project), lambda state: state["fresh"])
                yield process
                require(process.poll() is None, "Preview exited during workflow regression")
            finally:
                if process.poll() is None:
                    process.terminate()
                process.wait(timeout=10)

    def reliability(self):
        project = self.fixture("contention-652", count=652)
        with self.window(project) as process:
            compact = self.control(project, "pause")
            require("state" not in compact and len(json.dumps(compact)) < 2048, "Routine controls must return compact receipts")
            state = self.control(project, "snapshot", "--limit", 4096)["state"]
            require(state["entity_count"] == 652 and len(state["entities"]) == 652, "Large fixture entity count mismatch")
            query = self.live(project, "--ids", "player,marker", "--fields", "position", "--limit", 2)
            require([entry["id"] for entry in query["entities"]] == ["marker", "player"], "Entity filters must produce stable IDs")
            require(all(set(entry) == {"id", "position"} for entry in query["entities"]), "Field filter leaked full snapshots")
            def inspect_worker(_):
                for _ in range(self.iterations):
                    value = self.live(project, "--limit", 2)
                    require(value["fresh"] and value["returned_count"] == 2, "Concurrent inspection must remain fresh and complete")
            def control_worker(_):
                for _ in range(self.iterations // 2):
                    value = self.control(project, "step", "--steps", 1)
                    require("state" not in value, "Control grew into a full snapshot")
            with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
                pending = [pool.submit(inspect_worker, index) for index in range(4)]
                pending += [pool.submit(control_worker, index) for index in range(4)]
                for future in pending:
                    future.result()
            require(process.poll() is None, "Inspection stress killed preview")
            remaining = list((project / ".langmake/results").glob("*.json"))
            require(len(remaining) < 8, "Consumed CLI receipts accumulated")
            self.passed("652-entity concurrent inspection and controls stay alive with small consumed receipts")

            self.control(project, "resume")
            before = self.live(project)
            with deny_replacement(project / ".langmake/live.json"):
                stale = self.until(lambda: self.live(project), lambda value: not value["fresh"], timeout=6)
                require(stale["publication_sequence"] == before["publication_sequence"] or stale["age_ms"] >= 2500,
                        "Locked observation must expose its age")
                require(process.poll() is None, "A telemetry sharing violation killed gameplay")
            recovered = self.until(lambda: self.live(project), lambda value: value["fresh"] and value["tick"] > before["tick"] + 30)
            require(recovered["publication"]["failures"] > 0, "Publication failures must be observable")
            diagnostic = json.loads(recovered["publication"]["last_error"])
            require(diagnostic["os_error"] in (5, 32, 33) and diagnostic["operation"] == "replace", "Windows error code missing from snapshot diagnostics")
            self.passed("Replace-denying Windows reader makes snapshots stale without stopping gameplay, then recovers")

            self.control(project, "pause")
            state = self.control(project, "snapshot")["state"]
            request_id = str(time.time_ns())
            request = {"id": request_id, "session": state["session"], "action": "step", "steps": 7,
                       "expires_at_ms": int(time.time() * 1000) + 10000}
            request_path = project / ".langmake/commands" / (request_id + ".json")
            result_path = project / ".langmake/results" / (request_id + ".json")
            write_json(request_path, request)
            first = self.until(lambda: json.loads(result_path.read_text()), lambda value: value.get("ok", False))
            with deny_replacement(result_path):
                write_json(request_path, request)
                self.until(lambda: request_path.exists(), lambda exists: not exists)
            require(self.control(project, "snapshot")["state"]["tick"] == first["tick"] == state["tick"] + 7,
                    "Duplicate ID replayed a mutation while its receipt was locked")
            result_path.unlink()
            self.passed("A duplicate request with a locked receipt cannot apply its mutation twice")

            evidence = project / ".langmake/evidence/preserve.txt"
            evidence.parent.mkdir(exist_ok=True)
            evidence.write_text("requested evidence is not receipt cleanup")
            for index in range(1030):
                path = project / ".langmake/results" / ("expired-%04d.json" % index)
                path.write_text("{}")
                os.utime(path, (time.time() - 700, time.time() - 700))
            self.until(lambda: len(list((project / ".langmake/results").glob("expired-*.json"))), lambda count: count == 0, timeout=15)
            require(evidence.read_text() == "requested evidence is not receipt cleanup", "Receipt retention deleted evidence")
            self.passed("Expired abandoned receipts are bounded while requested evidence survives")

    def timelines(self):
        for dimension in ("2d", "3d"):
            project = self.fixture("timeline-" + dimension, dimension)
            timeline = self.directory / (dimension + "-input.json")
            write_json(timeline, {"events": [{"from": 0, "to": 4, "move_x": 1}, {"from": 2, "to": 5, "move_z": -1}]})
            output = self.directory / (dimension + "-captures")
            captured = self.run("capture", "--project", project, "--steps", 7, "--input", timeline,
                                "--ticks", "0,2,4,5,7", "--output-dir", output, "--no-overlay", timeout=60)
            require(len(captured["captures"]) == 5, "Every requested tick needs an artifact")
            for tick in (0, 2, 4, 5, 7):
                image = output / ("tick-%d.png" % tick)
                sidecar = json.loads(Path(str(image) + ".json").read_text())
                state = self.run("simulate", "--project", project, "--steps", tick, "--input", timeline)["state"]
                for key in ("tick", "entities", "camera", "scene", "state", "ui", "source_revision", "asset_revisions", "audio_events"):
                    require(sidecar[key] == state[key], "%s capture disagrees with simulation at tick %d (%s)" % (dimension, tick, key))
                require(not sidecar["overlay"] and len(sidecar["view_matrix"]) == 16 and image.read_bytes().startswith(b"\x89PNG"), "Capture metadata/pixels incomplete")
            self.run("capture", "--project", project, "--steps", 2, "--ticks", "2,1", "--output-dir", output, ok=False)
            self.run("capture", "--project", project, "--steps", 2, "--frames", 2, "--output-dir", output, ok=False)
            self.passed(dimension + " timeline captures match simulation at tick zero, overlaps, boundaries and completion")

    def player_and_ui(self):
        project = self.fixture("human-player", ui=True)
        with self.window(project, "play"):
            initial = self.live(project)
            for action in ("pause", "reset", "step"):
                self.control(project, action, ok=False)
            document = json.loads((project / "project.json").read_text())
            document["title"] = "Changed source must not reload human play"
            write_json(project / "project.json", document)
            later = self.until(lambda: self.live(project, "--check-source"),
                               lambda value: value["tick"] > initial["tick"] + 35 and not value["matches_source"])
            require(later["source_revision"] == initial["source_revision"] and later["mode"] == "play", "Player source edits restarted the game")
            capture = self.control(project, "capture")
            sidecar = json.loads(Path(capture["image"] + ".json").read_text())
            require(not sidecar["overlay"], "Player mode exposed developer overlay")
            isolated = self.directory / "isolated-beside-human"
            self.run("capture", "--project", project, "--steps", 0, "--ticks", "0", "--output-dir", isolated, "--no-overlay", timeout=60)
            require(self.live(project)["session"] == initial["session"], "Isolated capture replaced the human session")
            self.passed("Player mode rejects developer mutations and source watching while preserving observation")

        for width, height in ((1280, 800), (960, 600)):
            output = self.directory / ("hud-%d" % width)
            self.run("capture", "--project", project, "--steps", 0, "--ticks", "0", "--output-dir", output,
                     "--width", width, "--height", height, "--no-overlay", timeout=60)
            actual_width, actual_height, pixels = read_pixels(output / "tick-0.png")
            require((actual_width, actual_height) == (width, height), "UI capture dimensions differ")
            sample = pixels[65 * width + width - 40]
            require(sample[0] > 240 and 20 <= sample[1] <= 45 and 50 <= sample[2] <= 85,
                    "Screen-space HUD panel absent or incorrectly anchored: " + str(sample))
            require(json.loads((output / "tick-0.png.json").read_text())["ui"]["score"] == 7, "HUD values absent from observations")
        self.passed("HUD pixels and observable data remain anchored across resolutions without world-depth interference")

    def game_audio(self):
        project = self.fixture("audio-scenes")
        manifest = json.loads((project / "project.json").read_text())
        manifest.update({"schema_version": 2, "entry_scene": "one", "scenes": {"one": "scenes/one.json", "two": "scenes/two.json"},
                         "assets": {"note": {"type": "music", "path": "assets/note.music.json"}}})
        score = json.loads((ROOT / "examples/rockets-2d/assets/laser.sfx.json").read_text())
        write_json(project / "assets/note.music.json", score)
        write_json(project / "project.json", manifest)
        for scene, pan in (("one", -1), ("two", 1)):
            write_json(project / ("scenes/" + scene + ".json"), {"audio": {"tail_seconds": 0, "buses": {"cue": {"pan": pan}}}})
        (project / "scripts/game.luau").write_text("return {start=function() engine.play_music('note',false,'cue') end, update=function(dt) if engine.scene()=='one' and engine.tick()>=3 then engine.change_scene('two') end end}", encoding="utf-8")
        wav = self.directory / "scene-audio.wav"
        save = self.directory / "scene-audio-save.json"
        full = self.run("simulate", "--project", project, "--steps", 8, "--audio-output", wav, "--save-output", save)
        def samples(path):
            with wave.open(str(path), "rb") as stream:
                require(stream.getframerate() == 48000 and stream.getnchannels() == 2, "Game audio format differs")
                result = array.array("h", stream.readframes(stream.getnframes()))
                return result
        pcm = samples(wav)
        require(len(pcm) == 8 * 800 * 2 and full["state"]["scene"] == "two", "Multi-scene audio duration or transition differs")
        require(any(pcm[1200:3200:2]) and not any(pcm[1201:3200:2]), "First scene bus must pan left")
        require(any(pcm[8001:10000:2]) and not any(pcm[8000:10000:2]), "Scene mixer replacement must pan right")
        resumed_file = self.directory / "resumed-audio.wav"
        resumed = self.run("simulate", "--project", project, "--steps", 100, "--load-save", save, "--audio-output", resumed_file)
        resumed_pcm = samples(resumed_file)
        require(resumed["audio"]["sample_frames"] == 80000 and resumed["state"]["tick"] == 108, "Saved absolute ticks must normalize to scenario audio origin")
        require(not any(resumed_pcm[1200:4000:2]) and any(resumed_pcm[1201:4000:2]), "Saved music bus routing must persist")
        require(not any(resumed_pcm[-4000:]), "Saved non-looping music must remain non-looping")
        self.passed("Game audio follows scene bus changes and preserves saved routing, non-looping music and relative tick duration")

    def run_all(self):
        self.reliability()
        self.timelines()
        self.player_and_ui()
        self.game_audio()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", required=True, type=Path)
    parser.add_argument("--iterations", type=int, default=24)
    args = parser.parse_args()
    require(os.name == "nt", "Workflow graphics/file-sharing qualification requires Windows")
    require(2 <= args.iterations <= 200, "iterations must be 2..200")
    suite = Workflow(args.engine, args.iterations)
    try:
        suite.run_all()
        report = {"ok": True, "checks": suite.checks, "directory": str(suite.directory)}
    except Exception as error:
        report = {"ok": False, "checks": suite.checks, "error": repr(error), "directory": str(suite.directory)}
        write_json(suite.directory / "report.json", report)
        raise
    write_json(suite.directory / "report.json", report)
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
