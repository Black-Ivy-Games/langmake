#!/usr/bin/env python3
"""Win both shipped games using ordinary movement and jump inputs only.

Usage: python scripts/playthrough_tests.py --engine build/langmake.exe
The input recordings are examples/{2d,3d}/tests/win.json. Each replay runs twice
and must reproduce its final state on the tested build. This does not claim
bit-identical physics across different platforms or dependency versions.
All command results are retained in .cache/playthrough-tests/<unique run>.
Requires Python 3.9+ and only its standard library.
"""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time
import uuid


ROOT = Path(__file__).resolve().parents[1]


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", required=True, type=Path)
    arguments = parser.parse_args()
    engine = arguments.engine.resolve()
    stamp = time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:10]
    directory = ROOT / ".cache" / "playthrough-tests" / stamp
    directory.mkdir(parents=True, exist_ok=False)
    report = {"ok": False, "engine": str(engine), "commands": [], "games": []}
    started = time.monotonic()

    def simulate(project, steps, timeline=None):
        command = [str(engine), "simulate", "--project", str(project), "--steps", str(steps)]
        if timeline is not None:
            command.extend(["--input", str(timeline)])
        record = {"command": command, "cwd": str(directory)}
        report["commands"].append(record)
        result = subprocess.run(command, cwd=str(directory), capture_output=True,
                                text=True, encoding="utf-8", errors="replace", timeout=30)
        record.update({"exit_code": result.returncode, "stdout": result.stdout, "stderr": result.stderr})
        value = json.loads(result.stdout)
        require(result.returncode == 0 and value.get("ok") is True,
                "Simulation failed: " + result.stdout + result.stderr)
        require(value["state"]["tick"] == steps, "Simulation did not advance the requested step count")
        return value["state"]

    try:
        require(engine.is_file(), "Engine executable does not exist: " + str(engine))
        report["engine_sha256"] = digest(engine)
        for dimension in ("2d", "3d"):
            project = ROOT / "examples" / dimension
            manifest = project / "project.json"
            script = project / json.loads(manifest.read_text(encoding="utf-8"))["script"]
            timeline = project / "tests" / "win.json"
            before = {str(path.relative_to(ROOT)): digest(path) for path in (manifest, script, timeline)}
            recording = json.loads(timeline.read_text(encoding="utf-8"))
            steps = recording["steps"]
            require(type(steps) is int and 0 < steps <= 36000, "Replay requires a bounded integer step count")
            require(recording["fixed_dt"] == 1 / 60, "Replay must use the engine's fixed step")
            end = 0
            for event in recording["events"]:
                require(set(event).issubset({"from", "to", "move_x", "move_z", "jump"}),
                        "Replay may contain only ordinary input values")
                require(type(event["from"]) is int and type(event["to"]) is int and
                        end <= event["from"] < event["to"] <= steps,
                        "Replay event intervals must be ordered, nonoverlapping integer steps")
                end = event["to"]
                require(type(event.get("jump", False)) is bool, "Jump input must be boolean")
                for axis in ("move_x", "move_z"):
                    require(type(event.get(axis, 0)) in (int, float) and -1 <= event.get(axis, 0) <= 1,
                            "Movement input must remain in the ordinary controller range")

            idle = simulate(project, 30)
            require(idle["won"] is False and idle["collected"] == 0,
                    dimension + " must require player action to win")
            final = simulate(project, steps, timeline)
            require(final["won"] is True and final["collected"] == 5,
                    dimension + " replay must win and collect all five orbs")
            collectibles = [entity for entity in final["entities"] if entity["role"] == "collectible"]
            require(len(collectibles) == 5 and all(entity["collected"] and not entity["visible"]
                                                for entity in collectibles),
                    dimension + " must collect and hide every collectible entity")
            require(simulate(project, steps, timeline) == final,
                    dimension + " replay must reproduce the same final state on this build")
            after = {str(path.relative_to(ROOT)): digest(path) for path in (manifest, script, timeline)}
            require(before == after, "Playthrough must not modify the scene, script, or input recording")
            report["games"].append({"dimension": dimension, "steps": steps, "seconds": steps / 60,
                                    "source_sha256": before, "state": final})
            print("PASS {}: input-only win, 5/5 collected, {} steps ({:.2f}s), replay reproduced".format(
                dimension, steps, steps / 60), flush=True)
        report["ok"] = True
    except (AssertionError, OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        report["error"] = str(error)
        print("FAIL " + str(error), file=sys.stderr)
    finally:
        report["elapsed_seconds"] = round(time.monotonic() - started, 4)
        path = directory / "report.json"
        path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print("Report: " + str(path), flush=True)
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
