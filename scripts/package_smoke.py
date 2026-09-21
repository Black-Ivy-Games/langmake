"""Qualify portable Windows resources without compiler directories on PATH."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import uuid
import wave
import zipfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parent.parent
TEMPLATES = ["2d", "3d", "rockets-2d", "rockets-3d", "adventure", "strategy", "shared-2d", "workflow-2d"]


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=ROOT / "dist" / "Langmake-0.5.0")
    parser.add_argument("--no-archive", action="store_true")
    parser.add_argument("--archive", type=Path, help="New ZIP inside workspace/dist; preserves historical release archives")
    args = parser.parse_args()
    package = args.package.resolve()
    require(ROOT in package.parents, "Package must be inside the workspace")
    if args.archive:
        require(not args.no_archive, "--archive and --no-archive cannot be combined")
        require((ROOT / "dist") in args.archive.resolve().parents and args.archive.suffix.lower() == ".zip",
                "Archive must be a ZIP inside workspace/dist")
        require(not args.archive.exists(), "Explicit archive output must be new")
    work = ROOT / ".cache" / "package-smoke" / uuid.uuid4().hex
    work.mkdir(parents=True)
    env = os.environ.copy()
    env["PATH"] = ""
    env["TEMP"] = env["TMP"] = str(ROOT / ".cache" / "tmp")
    Path(env["TEMP"]).mkdir(parents=True, exist_ok=True)
    manifest = json.loads((package / "package-manifest.json").read_text(encoding="utf-8"))
    require(manifest["version"] == "0.5.0", "Expected Langmake 0.5.0 manifest")
    listed = set()
    for item in manifest["files"]:
        path = (package / item["path"]).resolve()
        require(package in path.parents, "Manifest path escapes package: " + item["path"])
        require(item["path"] not in listed, "Duplicate manifest entry")
        listed.add(item["path"])
        data = path.read_bytes()
        require(len(data) == item["bytes"] and hashlib.sha256(data).hexdigest() == item["sha256"], item["path"])
    # Extra user games are allowed in a used package but never enter its ZIP.
    for required in ["fonts/LatoLatin-Regular.ttf", "licenses/LatoLatin/LICENSE.txt", "licenses/FreeType/FTL.TXT",
                       "api/engine.d.luau", "api/model.schema.json", "RUNTIME-GUIDE.md", "AGENTS.md",
                       "GAME-WORKFLOW.md", "WORKFLOW-PROTOCOL.md", "docs/26-game-workflow.md",
                       "docs/25-workflow-implementation.md", "scripts/workflow/workflow.py",
                       "scripts/windows/windows.py", "scripts/mobile_assets.py",
                       "examples/workflow-2d/project.json", "examples/workflow-2d/assets/icon.png"]:
        require(required in listed, "Missing required resource: " + required)
    lock = json.loads((package / "dependencies.lock.json").read_text(encoding="utf-8"))
    require(lock["release"] == "0.5.0", "Dependency inventory release mismatch")
    for item in lock["license_files"] + lock["bundled_upstream_assets"]:
        require(item["path"] in listed, "Unpackaged license/resource: " + item["path"])
        require(hashlib.sha256((package / item["path"]).read_bytes()).hexdigest() == item["sha256"], item["path"])

    commands = []
    executable = package / "langmake.exe"

    def run(*arguments):
        result = subprocess.run([str(executable), *map(str, arguments)],
                                cwd=work, env=env, capture_output=True, text=True, timeout=120)
        try:
            response = json.loads(result.stdout)
        except json.JSONDecodeError as error:
            raise AssertionError((arguments, result.returncode, result.stdout, result.stderr)) from error
        commands.append({"arguments": list(map(str, arguments)), "exit_code": result.returncode})
        require(result.returncode == 0 and response.get("ok", True), (arguments, response, result.stderr))
        return response

    capabilities = run("capabilities")
    require(capabilities["version"] == "0.5.0", "Packaged executable version mismatch")
    require(capabilities == json.loads((package / "api/capabilities.json").read_text()), "Capabilities discovery differs")
    require(sorted(capabilities.get("starter_templates", [])) == sorted(TEMPLATES),
            "Native starter discovery must match all eight packaged and exercised templates")
    for starter in capabilities["starter_templates"]:
        require("examples/" + starter + "/project.json" in listed,
                "Discovered starter missing from package manifest: " + starter)
    require(run("schema") == json.loads((package / "api/project.schema.json").read_text()), "Project schema differs")
    checks = ["manifest, dependency license and bundled resource hashes match",
              "capabilities and project schema match with empty PATH from separate directory"]
    for asset_type in ["sprite", "mesh", "music", "model"]:
        discovered = run("asset", "schema", "--type", asset_type)
        require(discovered == json.loads((package / "api" / (asset_type + ".schema.json")).read_text()), asset_type)
    api = run("script-api")
    require(api == json.loads((package / "api/script-api.json").read_text()), "Script API differs")
    require(api["definitions"] == (package / "api/engine.d.luau").read_text(), "Generated Luau declarations differ")
    checks.append("four asset schemas and generated Luau API match runtime discovery")
    for starter in TEMPLATES:
        project = work / starter
        run("new", "--template", starter, "--output", project)
        run("validate", "--project", project)
        run("check", "--project", project)
        run("simulate", "--project", project, "--steps", 120)
        capture = run("capture", "--project", project, "--frames", 4, "--no-overlay", "--output", work / (starter + ".png"))
        require(capture["state"]["renderer"] == "Direct3D 11", "Unexpected capture backend")
        require((work / (starter + ".png")).read_bytes().startswith(b"\x89PNG\r\n\x1a\n"), "Missing PNG")
        require((work / (starter + ".png.json")).is_file(), "Missing capture state sidecar")
        checks.append(starter + " creates, validates, checks, simulates and renders with empty PATH")
        if starter in ["adventure", "strategy", "shared-2d", "workflow-2d"]:
            require(bool(capture["state"]["ui"]), starter + " UI values missing")
        if starter.startswith("rockets-"):
            assets = run("asset", "list", "--project", project)["assets"]
            require({item["id"] for item in assets} == {"rocket", "alien", "star", "planet", "theme", "laser"}, "Missing asset")
            run("asset", "build", "--project", project)
            graphic = work / (starter + ("-rocket.png" if starter.endswith("2d") else "-rocket.obj"))
            run("asset", "export", "--project", project, "--id", "rocket", "--output", graphic)
            if starter.endswith("2d"):
                require(graphic.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"), "Missing native sprite")
            else:
                require("\nv " in graphic.read_text() and "\nf " in graphic.read_text(), "Missing native mesh")
            for sound in ["theme", "laser"]:
                wav = work / (starter + "-" + sound + ".wav")
                run("asset", "render", "--project", project, "--id", sound, "--output", wav)
                with wave.open(str(wav), "rb") as stream:
                    require(stream.getnchannels() == 2 and stream.getframerate() == 48000 and stream.getsampwidth() == 2 and stream.getnframes() > 1000, "Invalid native music")
            checks.append(starter + " native graphics and synthesized audio export without external tools")
    run("capture", "--project", work / "adventure", "--steps", 2, "--ticks", "0,2", "--output-dir", work / "timeline", "--no-overlay")
    for tick in [0, 2]:
        sidecar = json.loads((work / "timeline" / ("tick-" + str(tick) + ".png.json")).read_text())
        require(sidecar["tick"] == tick, "Packaged timeline capture tick mismatch")
    audio = run("simulate", "--project", work / "adventure", "--steps", 60, "--audio-output", work / "game.wav")
    require((work / "game.wav").stat().st_size > 44 and bool(audio["audio"]), "Packaged game mixer failed")
    checks.append("bundled font HUD, exact tick capture and game audio render work without compiler tools")
    archive = args.archive.resolve() if args.archive else ROOT / "dist" / "Langmake-0.5.0-windows-x64.zip"
    if not args.no_archive:
        temporary_archive = work / "Langmake-0.5.0-windows-x64.zip"
        with zipfile.ZipFile(temporary_archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as output:
            for item in manifest["files"]:
                output.write(package / item["path"], "Langmake-0.5.0/" + item["path"])
            output.write(package / "package-manifest.json", "Langmake-0.5.0/package-manifest.json")
        temporary_archive.replace(archive)
        extracted = work / "extracted"
        with zipfile.ZipFile(archive) as source:
            for name in source.namelist():
                require(extracted in (extracted / name).resolve().parents, "Archive member escapes extraction directory")
            source.extractall(extracted)
        unpacked = extracted / "Langmake-0.5.0"
        for item in manifest["files"]:
            require(hashlib.sha256((unpacked / item["path"]).read_bytes()).hexdigest() == item["sha256"], "Extracted archive hash differs: " + item["path"])
        executable = unpacked / "langmake.exe"
        require(run("capabilities")["version"] == "0.5.0", "Extracted binary cannot run")
        run("new", "--template", "adventure", "--output", work / "extracted-game")
        run("validate", "--project", work / "extracted-game")
        run("check", "--project", work / "extracted-game")
        captured = run("capture", "--project", work / "extracted-game", "--steps", 0, "--ticks", "0", "--output-dir", work / "extracted-capture", "--no-overlay")
        require(bool(captured["state"]["ui"]), "Extracted package font/UI resources failed")
        checks.append("ZIP physically extracts, verifies every file and creates/checks/renders a game with empty PATH")
    report = {"ok": True, "checks": checks, "package": str(package), "work": str(work),
              "archive": None if args.no_archive else str(archive), "files": len(manifest["files"]),
              "commands": commands,
              "executable_sha256": hashlib.sha256((package / "langmake.exe").read_bytes()).hexdigest()}
    (work / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
