#!/usr/bin/env python3
"""Exercise the shipped Langmake CLI; all writable fixtures stay in this repo.

Usage: python scripts/integration_tests.py --engine build/langmake.exe [--graphics]
Requires Python 3.9+ and only its standard library. Runs are retained beneath
.cache/integration-tests so failed command outputs and projects can be inspected.
"""

import argparse
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import time
import uuid
import zlib


ROOT = Path(__file__).resolve().parents[1]
DT = 1.0 / 60.0


def require(condition, description):
    if not condition:
        raise AssertionError(description)


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def entity(snapshot, entity_id):
    return next(item for item in snapshot["entities"] if item["id"] == entity_id)


def diagnostic_text(value):
    return json.dumps(value, ensure_ascii=False)


class Suite:
    def __init__(self, engine, graphics):
        self.engine = engine.resolve()
        self.graphics = graphics
        require(self.engine.is_file(), "Engine executable does not exist: " + str(self.engine))
        stamp = time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:10]
        self.directory = ROOT / ".cache" / "integration-tests" / stamp
        self.directory.mkdir(parents=True, exist_ok=False)
        self.commands = []
        self.completed = []
        self.started = time.monotonic()

    def run(self, *arguments, ok=True, timeout=30):
        command = [str(self.engine)] + [str(arg) for arg in arguments]
        started = time.monotonic()
        record = {"command": command, "cwd": str(self.directory), "expected_success": ok}
        self.commands.append(record)
        try:
            process = subprocess.run(command, cwd=str(self.directory), capture_output=True,
                                     text=True, encoding="utf-8", errors="replace", timeout=timeout)
        except subprocess.TimeoutExpired as error:
            record["error"] = "Subprocess timed out after {} seconds".format(timeout)
            raise AssertionError(record["error"] + ": " + " ".join(command)) from error
        record.update({"exit_code": process.returncode,
                       "elapsed_seconds": round(time.monotonic() - started, 4),
                       "stdout": process.stdout, "stderr": process.stderr})
        try:
            result = json.loads(process.stdout)
        except json.JSONDecodeError as error:
            raise AssertionError("CLI did not emit one JSON document: {}\nstdout={}\nstderr={}".format(
                " ".join(command), process.stdout[:4000], process.stderr[:4000])) from error
        require(isinstance(result, dict), "CLI response must be a JSON object")
        if ok:
            require(process.returncode == 0 and result.get("ok", True) is not False,
                    "Command failed: {}\n{}".format(" ".join(command), diagnostic_text(result)))
        else:
            require(process.returncode != 0 and result.get("ok") is False,
                    "Command must reject invalid input: " + " ".join(command))
            require(isinstance(result.get("diagnostics"), list) and result["diagnostics"],
                    "Rejected command must include structured diagnostics")
        return result

    def passed(self, name):
        self.completed.append(name)
        print("PASS " + name, flush=True)

    def fixture(self, name, dimension):
        project = self.directory / name
        project.mkdir()
        (project / "scripts").mkdir()
        document = {
            "schema_version": 1, "dimension": dimension, "title": "Integration " + dimension,
            "script": "scripts/game.luau", "gravity": [0, -18, 0],
            "camera": {"position": [10, 10, 16], "target": [0, 1, 0]},
            "entities": [
                {"id": "floor", "shape": "box", "body": "static", "role": "decoration",
                 "position": [0, -0.5, 0], "size": [100, 1, 100], "color": [0.2, 0.3, 0.4, 1]},
                {"id": "player", "shape": "box", "body": "dynamic", "role": "player",
                 "position": [0, 3, 0], "size": [1, 1, 1], "color": [0.9, 0.5, 0.2, 1], "mass": 1},
                {"id": "coin", "shape": "sphere", "body": "none", "role": "collectible",
                 "position": [6, 1, 0], "size": [0.5, 0.5, 0.5], "color": [1, 0.8, 0.1, 1]}
            ]
        }
        write_json(project / "project.json", document)
        (project / "scripts" / "game.luau").write_text('''return {
    start = function() engine.message("integration ready") end,
    update = function(dt)
        engine.move_player(engine.input("move_x") * 5, engine.input("move_z") * 5)
        if engine.input("jump") then engine.jump(7) end
    end,
}
''', encoding="utf-8")
        return project

    def smoke_and_discovery(self):
        for dimension in ("2d", "3d"):
            project = ROOT / "examples" / dimension
            valid = self.run("validate", "--project", project)
            require(valid["dimension"] == dimension and valid["entities"] > 0,
                    "Bundled starter must initialize in " + dimension)
            inspected = self.run("inspect", "--project", project)
            require(inspected["dimension"] == dimension and inspected["revision"] == valid["revision"],
                    "inspect and validate must identify the same authored project")
            require(len(inspected["entities"]) == inspected["entity_count"], "inspect entity count must match")
        capabilities = self.run("capabilities")
        require(set(capabilities["dimensions"]) == {"2d", "3d"}, "Both dimensions must be discoverable")
        require(capabilities["scripts"]["language"] == "Luau", "Scripting language must be discoverable")
        require("move_player" in capabilities["scripts"]["api"], "Scripting API must be discoverable")
        expected_starters = {"2d", "3d", "rockets-2d", "rockets-3d", "adventure", "strategy", "shared-2d", "workflow-2d"}
        require(set(capabilities["starter_templates"]) == expected_starters, "All eight supported starters must be discoverable")
        for starter in capabilities["starter_templates"]:
            created = self.directory / ("discovered-" + starter)
            self.run("new", "--template", starter, "--output", created)
            require((created / "project.json").is_file(), "Advertised starter must be creatable: " + starter)
        session_help = self.run("help")["commands"]["session"]
        require(all(word in session_help for word in ("show", "release", "protect", "launch", "--target", "--development", "--fresh")),
                "Session help must document protection, mobile launch and bounded verification flags")
        schema = self.run("schema")
        require(schema["type"] == "object" and "entities" in schema["properties"], "Scene schema must describe entities")
        require(schema.get("additionalProperties") is False, "Project schema must reject unknown fields")
        self.run("inspect", "--nonsense", "true", ok=False)
        self.run("simulate", "--project", ROOT / "examples" / "2d", "--steps", "-1", ok=False)
        self.run("no_such_command", ok=False)
        missing = self.run("new", "--output", ok=False)
        require("requires a nonempty value" in diagnostic_text(missing), "Missing path must not become a literal 'true' directory")
        self.run("simulate", "--steps", "--project", ROOT / "examples" / "2d", ok=False)
        for flag, command in (("--live", "inspect"), ("--no-audio", "preview")):
            rejected = self.run(command, "--project", ROOT / "examples" / "2d", flag, "false", ok=False)
            require("Flag does not accept a value" in diagnostic_text(rejected), "Boolean flag values must be rejected explicitly")
        zero_capture = self.directory / "capture-must-not-exist.png"
        capture = self.run("capture", "--project", ROOT / "examples" / "2d", "--frames", "0", "--output", zero_capture,
                           ok=False, timeout=10)
        require("positive --frames" in diagnostic_text(capture), "Automated capture must not accept unlimited frames")
        require(not zero_capture.exists(), "Rejected capture must not render or create an image")
        self.passed("bundled 2D/3D validation, inspection, schema, capabilities and CLI rejection")

    def simulation(self):
        for dimension in ("2d", "3d"):
            project = self.fixture("physics-" + dimension, dimension)
            baseline = self.run("simulate", "--project", project, "--steps", "120")
            state = baseline["state"]
            require(state["tick"] == 120 and math.isclose(baseline["fixed_dt"], DT, abs_tol=1e-12),
                    "Simulation must use the requested number of fixed ticks")
            resting = entity(state, "player")["position"]
            require(0.35 < resting[1] < 0.65 and state["grounded"],
                    dimension + " player must fall and settle on physics ground")
            require(state["message"] == "integration ready", "Luau start() must execute")
            timeline = project / "timeline.json"
            write_json(timeline, {"events": [{"from": 60, "to": 90, "move_x": 1,
                                             "move_z": 1 if dimension == "3d" else 0, "jump": False}]})
            evidence = project / "simulation.json"
            moved = self.run("simulate", "--project", project, "--steps", "90", "--input", timeline,
                             "--output", evidence)
            repeated = self.run("simulate", "--project", project, "--steps", "90", "--input", timeline)
            require(moved["state"] == repeated["state"],
                    dimension + " fixed inputs must reproduce state in repeated runs on this build")
            require(json.loads(evidence.read_text(encoding="utf-8")) == moved,
                    "Simulation output file must match the returned machine evidence")
            position = entity(moved["state"], "player")["position"]
            require(position[0] > 1.5, dimension + " timeline input must drive Luau movement")
            if dimension == "3d":
                require(position[2] > 1.5, "3D movement must include the z axis")
            else:
                require(position[2] == 0, "2D movement must preserve z")
            write_json(timeline, {"events": [{"from": 120, "to": 121, "jump": True}]})
            jumped = self.run("simulate", "--project", project, "--steps", "130", "--input", timeline)
            height = entity(jumped["state"], "player")["position"][1]
            require(resting[1] + 0.5 < height < resting[1] + 1.8,
                    dimension + " a grounded jump must lift the player")
            require(not jumped["state"]["grounded"], "Airborne player must not count as grounded")
            landed = self.run("simulate", "--project", project, "--steps", "240", "--input", timeline)
            require(landed["state"]["grounded"], dimension + " jump must return to the floor")
            write_json(timeline, {"events": [{"from": -1, "to": 2}]})
            self.run("simulate", "--project", project, "--input", timeline, ok=False)
            write_json(timeline, {"events": [{"from": 0, "to": 2, "move_x": 2}]})
            self.run("simulate", "--project", project, "--input", timeline, ok=False)
            for from_tick, to_tick in ((0.5, 2), (0, 2.5), (True, 2), (0, 2**64 - 1)):
                write_json(timeline, {"events": [{"from": from_tick, "to": to_tick}]})
                rejected = self.run("simulate", "--project", project, "--input", timeline, ok=False)
                require("Input event" in diagnostic_text(rejected), "Invalid integer ticks must have useful diagnostics")
            self.passed(dimension + " physics, fixed-step reproducibility, timeline movement, jump and evidence output")

    def mutations(self):
        project = self.fixture("mutations", "2d")
        manifest = project / "project.json"
        script = project / "scripts" / "game.luau"
        before = self.run("inspect", "--project", project)
        patch = project / "patch.json"
        write_json(patch, {"expected_revision": before["revision"], "changes": [
            {"id": "player", "property": "color", "value": [0.1, 0.7, 0.2, 1]}]})
        changed = self.run("patch", "--project", project, "--file", patch)
        after = self.run("inspect", "--project", project)
        require(after["revision"] == changed["revision"] != before["revision"], "Successful edit must change revision")
        require(all(math.isclose(actual, expected, abs_tol=1e-6) for actual, expected in
                    zip(entity(after, "player")["color"], [0.1, 0.7, 0.2, 1])),
                "Patch must update the requested property")
        saved_bytes = manifest.read_bytes()
        stale = self.run("patch", "--project", project, "--file", patch, ok=False)
        require("revision_conflict" in diagnostic_text(stale), "Stale revisions must identify their conflict")
        require(manifest.read_bytes() == saved_bytes, "Rejected stale patch must not write anything")
        write_json(patch, {"expected_revision": after["revision"], "changes": [
            {"id": "player", "property": "color", "value": [0.8, 0.1, 0.4, 1]},
            {"id": "floor", "property": "size", "value": [-1, 1, 1]}]})
        self.run("patch", "--project", project, "--file", patch, ok=False)
        require(manifest.read_bytes() == saved_bytes, "Patch validation failure must leave all properties unchanged")
        require(self.run("inspect", "--project", project)["revision"] == after["revision"],
                "Failed transaction must preserve revision")
        checkpoint_manifest = manifest.read_bytes()
        checkpoint_script = script.read_bytes()
        checkpoint = self.run("checkpoint", "--project", project, "--name", "known-good")
        self.run("checkpoint", "--project", project, "--name", "known-good", ok=False)
        script.write_bytes(checkpoint_script + b"\n-- intentional script change\n")
        script_changed = self.run("inspect", "--project", project)
        require(script_changed["revision"] != after["revision"], "Revision must include script changes")
        write_json(patch, {"expected_revision": after["revision"], "changes": [
            {"id": "player", "property": "name", "value": "stale script edit"}]})
        self.run("patch", "--project", project, "--file", patch, ok=False)
        write_json(patch, {"expected_revision": script_changed["revision"], "changes": [
            {"id": "player", "property": "name", "value": "changed after checkpoint"}]})
        self.run("patch", "--project", project, "--file", patch)
        restored = self.run("restore", "--project", project, "--name", "known-good")
        require(restored["revision"] == checkpoint["revision"], "Restore must recover the checkpoint revision")
        require(manifest.read_bytes() == checkpoint_manifest and script.read_bytes() == checkpoint_script,
                "Restore must recover both authored manifest and Luau script byte-for-byte")
        self.run("validate", "--project", project)
        self.passed("revision-checked patch, stale rejection, atomic validation failure and checkpoint/script restore")

    def script_diagnostics(self):
        project = self.fixture("script-errors", "2d")
        script = project / "scripts" / "game.luau"
        script.write_text("return {\n", encoding="utf-8")
        syntax = self.run("validate", "--project", project, ok=False)
        require("game.luau" in diagnostic_text(syntax), "Syntax errors must identify the source file")
        require(syntax["diagnostics"][0].get("phase") == "compile", "Compile diagnostic fields must remain structured")
        script.write_text("return {update=function(dt) error('integration runtime failure') end}\n", encoding="utf-8")
        runtime = self.run("simulate", "--project", project, "--steps", "1", ok=False)
        require("integration runtime failure" in diagnostic_text(runtime), "Runtime errors must preserve the script message")
        require("game.luau" in diagnostic_text(runtime), "Runtime errors must identify the source file")
        require(runtime["diagnostics"][0].get("phase") == "update" and runtime["diagnostics"][0].get("tick") == 0,
                "Runtime diagnostic phase and tick must be directly readable fields")
        script.write_text("while true do end\n", encoding="utf-8")
        bounded = self.run("validate", "--project", project, ok=False, timeout=10)
        require("SCRIPT_BUDGET_EXCEEDED" in diagnostic_text(bounded), "Infinite scripts must be interrupted by the VM budget")
        require(bounded["diagnostics"][0].get("code") == "SCRIPT_BUDGET_EXCEEDED", "VM errors must not be nested JSON strings")
        script.write_text("return {update=function() pcall(function() while true do end end) end}\n", encoding="utf-8")
        caught = self.run("simulate", "--project", project, "--steps", "1", ok=False, timeout=10)
        require(caught["diagnostics"][0].get("code") == "SCRIPT_BUDGET_EXCEEDED",
                "Catching a script timeout must not bypass the host budget or access an empty VM stack")
        self.passed("Luau compile/runtime diagnostics and bounded infinite-loop rejection")

    def creation_and_capture(self):
        for dimension in ("2d", "3d"):
            output = self.directory / ("new-" + dimension)
            result = self.run("new", "--dimension", dimension, "--output", output)
            require(Path(result["project"]).resolve() == output / "project.json", "new must identify the created project")
            valid = self.run("validate", "--project", output)
            require(valid["dimension"] == dimension, "New project must match the selected template dimension")
            original = (output / "project.json").read_bytes()
            self.run("new", "--dimension", dimension, "--output", output, ok=False)
            require((output / "project.json").read_bytes() == original, "new must preserve an existing directory")
            if self.graphics:
                png = self.directory / ("capture-" + dimension + ".png")
                self.run("capture", "--project", output, "--frames", "4", "--width", "960", "--height", "600",
                         "--output", png, timeout=60)
                validate_png(png, 960, 600)
        self.passed("2D/3D starter creation and overwrite protection" + (", rendered PNG evidence" if self.graphics else ""))

    def report(self, error=None):
        value = {"ok": error is None, "engine": str(self.engine), "fixtures": str(self.directory),
                 "graphics": self.graphics, "completed": self.completed, "commands": self.commands,
                 "elapsed_seconds": round(time.monotonic() - self.started, 4)}
        if error is not None:
            value["error"] = str(error)
        write_json(self.directory / "report.json", value)
        return self.directory / "report.json"


def validate_png(path, width, height):
    require(path.is_file(), "Capture did not create a PNG: " + str(path))
    data = path.read_bytes()
    require(data[:8] == b"\x89PNG\r\n\x1a\n", "Capture must have a PNG signature")
    offset = 8
    image_data = bytearray()
    found_header = False
    found_end = False
    bit_depth = color_type = interlace = None
    while offset + 12 <= len(data):
        size = struct.unpack(">I", data[offset:offset + 4])[0]
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + size]
        require(offset + size + 12 <= len(data), "PNG chunk is truncated")
        expected_crc = struct.unpack(">I", data[offset + 8 + size:offset + 12 + size])[0]
        require(zlib.crc32(kind + payload) & 0xffffffff == expected_crc, "PNG CRC is invalid")
        if kind == b"IHDR":
            require(len(payload) == 13 and not found_header, "PNG must have one valid IHDR chunk")
            actual_width, actual_height, bit_depth, color_type, compression, filtering, interlace = struct.unpack(
                ">IIBBBBB", payload)
            require((actual_width, actual_height) == (width, height), "PNG dimensions must match requested capture size")
            allowed_depths = {0: (1, 2, 4, 8, 16), 2: (8, 16), 3: (1, 2, 4, 8), 4: (8, 16), 6: (8, 16)}
            require(color_type in allowed_depths and bit_depth in allowed_depths[color_type],
                    "PNG color type and bit depth must form a valid combination")
            require(compression == 0 and filtering == 0 and interlace in (0, 1),
                    "PNG uses an invalid compression, filtering, or interlace method")
            found_header = True
        elif kind == b"IDAT":
            image_data.extend(payload)
        elif kind == b"IEND":
            found_end = True
            break
        offset += size + 12
    require(found_header and found_end and image_data, "PNG must contain complete image chunks")
    pixels = zlib.decompress(bytes(image_data))
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color_type]
    bits_per_pixel = channels * bit_depth
    # LodePNG can choose indexed color and pack several pixels into one byte.
    # Count actual filtered scanline bytes rather than assuming RGB8 storage.
    passes = ((0, 0, 1, 1),) if interlace == 0 else (
        (0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4),
        (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2))
    expected_size = 0
    row_layouts = []
    for start_x, start_y, step_x, step_y in passes:
        pass_width = max(0, (width - start_x + step_x - 1) // step_x)
        pass_height = max(0, (height - start_y + step_y - 1) // step_y)
        if pass_width and pass_height:
            row_size = (pass_width * bits_per_pixel + 7) // 8 + 1
            expected_size += row_size * pass_height
            row_layouts.append((row_size, pass_height))
    require(len(pixels) == expected_size, "PNG decoded scanline size does not match its IHDR")
    offset = 0
    for row_size, rows in row_layouts:
        for _ in range(rows):
            require(pixels[offset] <= 4, "PNG scanline has an invalid filter type")
            offset += row_size


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", required=True, type=Path)
    parser.add_argument("--graphics", action="store_true", help="Also open windows and verify rendered PNG captures")
    arguments = parser.parse_args()
    suite = Suite(arguments.engine, arguments.graphics)
    print("Fixtures: " + str(suite.directory), flush=True)
    try:
        suite.smoke_and_discovery()
        suite.simulation()
        suite.mutations()
        suite.script_diagnostics()
        suite.creation_and_capture()
    except (AssertionError, OSError, ValueError, KeyError, StopIteration, subprocess.SubprocessError) as error:
        report = suite.report(error)
        print("FAIL {}\nReport: {}".format(error, report), file=sys.stderr)
        return 1
    report = suite.report()
    print("All {} integration checks passed. Report: {}".format(len(suite.completed), report))
    return 0


if __name__ == "__main__":
    sys.exit(main())
