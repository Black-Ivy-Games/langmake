"""Player-bundle regressions; artifacts stay under the workspace .cache."""
import argparse
import json
import itertools
import os
from pathlib import Path
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", type=Path, default=ROOT / "build/langmake.exe")
    args = parser.parse_args()
    engine = args.engine.resolve()
    work = ROOT / ".cache/cook-tests" / uuid.uuid4().hex
    work.mkdir(parents=True)
    checks = []

    def run(*command, ok=True):
        result = subprocess.run([str(engine), *map(str, command)], cwd=ROOT,
                                capture_output=True, text=True, timeout=120)
        value = json.loads(result.stdout)
        assert bool(value.get("ok")) == ok, (command, result.stdout, result.stderr)
        assert (result.returncode == 0) == ok, (command, result.returncode)
        return value

    for template, target in itertools.product(
            ("2d", "3d", "rockets-2d", "rockets-3d", "adventure", "strategy", "shared-2d"),
            ("windows", "android", "ios", "macos")):
        source, output = work / template / target / "source", work / template / target / "bundle"
        run("new", "--template", template, "--output", source)
        # Project-adjacent user history and runtime secrets must not be shipped.
        (source / ".langmake").mkdir(exist_ok=True)
        (source / ".langmake/private.json").write_text('{"secret":"do-not-package"}')
        (source / "chat-history.json").write_text('{"private":"do-not-package"}')
        run("cook", "--project", source, "--target", target, "--output", output)
        manifest = json.loads((output / "cook-manifest.json").read_text())
        assert manifest["target"] == target
        actual = {p.relative_to(output).as_posix() for p in output.rglob("*") if p.is_file()}
        expected = {f["path"] for f in manifest["files"]} | {"cook-manifest.json"}
        assert actual == expected and "chat-history.json" not in actual
        assert not (output / ".langmake").exists()
        assert manifest["content_encoding"] == "validated-source-graph"
        assert manifest["source_revision"] == run("inspect", "--project", source)["revision"]
        for item in manifest["files"]:
            assert (output / item["path"]).stat().st_size == item["bytes"]
        run("validate", "--project", output)
        original = run("simulate", "--project", source, "--steps", "30")["state"]
        bundled = run("simulate", "--project", output, "--steps", "30")["state"]
        for key in ("tick", "scene", "state", "entities", "camera", "won", "collected"):
            if key in original:
                assert original[key] == bundled[key], (template, key)
        before = (output / "project.json").read_bytes()
        run("cook", "--project", source, "--target", target, "--output", output, ok=False)
        assert (output / "project.json").read_bytes() == before
        checks.append({"template": template, "target": target, "source_files": len(expected) - 1,
                       "isolated_content": True, "source_player_parity": True,
                       "existing_output_preserved": True})

    source = work / "2d/ios/source"
    bad_output = work / "bad-output"
    run("cook", "--project", source, "--target", "unknown", "--output", bad_output, ok=False)
    assert not bad_output.exists()
    manifest_path = source / "project.json"
    doc = json.loads(manifest_path.read_text())
    script = source / doc["script"]
    script.write_text("this cannot compile as Luau")
    run("cook", "--project", source, "--target", "android", "--output", bad_output, ok=False)
    assert not bad_output.exists()
    report = {"ok": True, "checks": checks, "invalid_target_and_sources_preserve_output": True,
              "engine": str(engine), "work": str(work)}
    (work / "report.json").write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
