"""Scene catalog replacement regression fixture.

Creates an isolated project whose room scene replaces the root asset, module,
definition and prefab registries. The script drives native commands and leaves
the work folder intact on failure for inspection.
"""
import argparse
import json
import shutil
import subprocess
import sys
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def write_text(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(value, encoding="utf-8")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def scene_entities(save_path, scene):
    saved = json.loads(save_path.read_text(encoding="utf-8"))
    return saved.get("scene_states", {}).get(scene, {}).get("entities", [])


def require_room_entity(save_path, spawned_id, message):
    require(any(entity.get("id") == spawned_id and entity.get("asset") == "only_room"
                for entity in scene_entities(save_path, "room")), message)


class Harness:
    def __init__(self, engine, work):
        self.engine = engine
        self.work = work
        self.checks = []

    def run(self, *command, ok=True, timeout=120):
        process = subprocess.run([str(self.engine), *map(str, command)], cwd=ROOT,
                                 capture_output=True, text=True, timeout=timeout)
        try:
            value = json.loads(process.stdout)
        except json.JSONDecodeError as error:
            raise AssertionError({
                "command": command,
                "returncode": process.returncode,
                "stdout": process.stdout,
                "stderr": process.stderr,
            }) from error
        if bool(value.get("ok")) != ok or ((process.returncode == 0) != ok):
            raise AssertionError({
                "command": command,
                "expected_ok": ok,
                "returncode": process.returncode,
                "stdout": value,
                "stderr": process.stderr,
            })
        return value

    def copy_fixture(self, destination):
        if destination.exists():
            raise FileExistsError(destination)
        shutil.copytree(self.project, destination)

    def sprite(self, name, color):
        write_json(self.project / f"assets/{name}.sprite.json", {
            "schema_version": 1,
            "width": 2,
            "height": 2,
            "palette": {"X": color},
            "frames": [{
                "id": "idle",
                "pixels": ["XX", "XX"],
            }],
        })

    def music(self, name):
        write_json(self.project / f"assets/{name}.music.json", {
            "schema_version": 1,
            "type": "music",
            "title": name,
            "bpm": 120,
            "ppq": 96,
            "length": 24,
            "gain": 0.25,
            "instruments": {
                "ping": {
                    "wave": "sine",
                    "gain": 0.15,
                    "attack": 0.002,
                    "decay": 0.05,
                    "sustain": 0,
                    "release": 0.01,
                }
            },
            "notes": [{"tick": 0, "duration": 12, "note": 72, "instrument": "ping", "velocity": 1}],
        })

    def create_fixture(self):
        self.project = self.work / "fixture"
        if self.project.exists():
            raise FileExistsError(self.project)
        for folder in ("assets", "data", "modules", "prefabs", "scenes", "scripts"):
            (self.project / folder).mkdir(parents=True, exist_ok=True)

        self.sprite("shared_root", "#FF0000FF")
        self.sprite("only_root", "#00FF00FF")
        self.sprite("shared_room", "#0000FFFF")
        self.sprite("only_room", "#FFFF00FF")
        self.music("room_ping")

        write_text(self.project / "modules/value_root.luau", "return {n=1}\n")
        write_text(self.project / "modules/value_room.luau", "return {n=2}\n")
        write_json(self.project / "data/root.json", {"marker": "root"})
        write_json(self.project / "data/room.json", {"marker": "room"})
        write_json(self.project / "prefabs/root.json", {
            "id": "root_prefab",
            "position": [0, 0, 0],
            "size": [1, 1, 0.1],
            "asset": "only_root",
            "body": "none",
        })
        write_json(self.project / "prefabs/room.json", {
            "id": "room_prefab",
            "position": [1, 0, 0],
            "size": [1, 1, 0.1],
            "asset": "only_room",
            "body": "none",
        })

        write_text(self.project / "scripts/root.luau", """
return {
 update=function(dt)
  if #engine.ui_event('go','click')>0 then engine.change_scene('room') end
 end
}
""".lstrip())
        write_text(self.project / "scripts/room.luau", """
local value=require('value')
return {
 start=function()
  local defs=engine.definitions()
  engine.state_set('number',value.n)
  engine.state_set('marker',defs.data.marker)
  local spawned=engine.state_get('spawned_id','')
  if spawned=='' or not engine.exists(spawned) then
   spawned=engine.spawn('local')
   engine.state_set('spawned_id',spawned)
  end
  engine.play_sound('room_ping')
 end,
 update=function(dt)
  if #engine.ui_event('home','click')>0 then engine.change_scene('root') end
 end
}
""".lstrip())

        write_json(self.project / "scenes/root.json", {})
        write_json(self.project / "scenes/room.json", {
            "script": "scripts/room.luau",
            "assets": {
                "shared": {"type": "sprite", "path": "assets/shared_room.sprite.json"},
                "only_room": {"type": "sprite", "path": "assets/only_room.sprite.json"},
                "room_ping": {"type": "music", "path": "assets/room_ping.music.json"},
            },
            "modules": {"value": "modules/value_room.luau"},
            "definitions": {"data": "data/room.json"},
            "prefabs": {"local": "prefabs/room.json"},
            "entities": [],
            "audio": {"tail_seconds": 0},
        })
        write_json(self.project / "project.json", {
            "schema_version": 2,
            "project_id": "scene-catalog-tests",
            "title": "Scene Catalog Tests",
            "dimension": "2d",
            "script": "scripts/root.luau",
            "entry_scene": "root",
            "scenes": {
                "root": "scenes/root.json",
                "room": "scenes/room.json",
            },
            "assets": {
                "shared": {"type": "sprite", "path": "assets/shared_root.sprite.json"},
                "only_root": {"type": "sprite", "path": "assets/only_root.sprite.json"},
            },
            "modules": {"value": "modules/value_root.luau"},
            "definitions": {"data": "data/root.json"},
            "prefabs": {"root": "prefabs/root.json"},
            "entities": [],
            "save_version": 1,
        })
        write_json(self.work / "room_roundtrip.input.json", {
            "events": [
                {"from": 1, "to": 2, "events": [{"type": "ui", "id": "go", "event": "click", "value": ""}]},
                {"from": 8, "to": 9, "events": [{"type": "ui", "id": "home", "event": "click", "value": ""}]},
            ]
        })
        write_json(self.work / "home_room.input.json", {
            "events": [
                {"from": 1, "to": 2, "events": [{"type": "ui", "id": "go", "event": "click", "value": ""}]}
            ]
        })
        write_json(self.work / "go_room.input.json", {
            "events": [
                {"from": 1, "to": 2, "events": [{"type": "ui", "id": "go", "event": "click", "value": ""}]}
            ]
        })

    def assert_initial_commands(self):
        checked = self.run("check", "--project", self.project)
        require(checked.get("ok") is True, "initial check failed")
        validated = self.run("validate", "--project", self.project)
        revision = validated.get("revision") or self.run("inspect", "--project", self.project)["revision"]
        require(isinstance(revision, str) and revision, "validate/inspect did not report revision")
        self.checks.append("check and validate succeed on isolated scene fixture")
        return revision

    def assert_scene_runtime(self):
        audio = self.work / "roundtrip.wav"
        save = self.work / "scene-save.json"
        self.initial_save = save
        result = self.run("simulate", "--project", self.project, "--steps", "12",
                          "--input", self.work / "room_roundtrip.input.json",
                          "--save-output", save, "--audio-output", audio)
        state = result["state"]
        require(state["scene"] == "root", "room home event did not return to root")
        require(state["state"]["number"] == 2 and state["state"]["marker"] == "room",
                "room scene did not use replacement module/definition registries")
        spawned_id = state["state"]["spawned_id"]
        require(isinstance(spawned_id, str) and spawned_id, "room did not store generated spawn ID")
        require_room_entity(save, spawned_id,
                            "inactive room entity with room-only visual was not preserved in root save JSON")
        root_shared = state["asset_revisions"]["shared"]
        require(audio.is_file() and audio.stat().st_size > 44, "audio output missing across scene transition")
        restored_save = self.work / "restored-root-save.json"
        restored = self.run("simulate", "--project", self.project, "--steps", "0", "--load-save", save,
                            "--save-output", restored_save)
        require(restored["state"]["scene"] == "root", "fresh restore did not preserve saved root scene")
        require(restored["state"]["state"]["spawned_id"] == spawned_id, "fresh root restore lost generated spawn ID")
        require_room_entity(restored_save, spawned_id,
                            "fresh root restore dropped inactive room entity with room-only visual")
        returned_save = self.work / "returned-room-save.json"
        restored_room = self.run("simulate", "--project", self.project, "--steps", "5",
                                 "--load-save", save, "--input", self.work / "home_room.input.json",
                                 "--audio-output", self.work / "home-room-restored.wav",
                                 "--save-output", returned_save)
        require(restored_room["state"]["scene"] == "room", "return to room after fresh restore failed")
        require(any(entity["id"] == spawned_id and entity["asset"] == "only_room"
                    for entity in restored_room["state"].get("entities", [])),
                "return to room after fresh restore did not preserve room-only visual entity")
        require_room_entity(returned_save, spawned_id,
                            "return to room second save did not preserve room-only visual entity")
        require(restored_room["state"]["asset_revisions"]["shared"] != root_shared,
                "shared asset did not switch from root recipe to room recipe")

        room_save = self.work / "room-save.json"
        room_audio = self.work / "room.wav"
        room = self.run("simulate", "--project", self.project, "--steps", "5",
                        "--input", self.work / "go_room.input.json",
                        "--save-output", room_save, "--audio-output", room_audio)
        require(room["state"]["scene"] == "room", "go event did not enter room scene")
        require(room["state"]["state"]["number"] == 2 and room["state"]["state"]["marker"] == "room",
                "room state after save-output is not from replacement graph")
        require(room["state"]["asset_revisions"]["shared"] != root_shared,
                "room shared asset revision matched root shared asset revision")
        restored_active_room = self.run("simulate", "--project", self.project, "--steps", "0", "--load-save", room_save,
                                        "--audio-output", self.work / "room-restored.wav")
        require(restored_active_room["state"]["scene"] == "room", "fresh restore did not preserve room scene")
        require(restored_active_room["state"]["asset_revisions"]["shared"] == room["state"]["asset_revisions"]["shared"],
                "active room restore did not preserve room shared asset recipe")
        self.checks.append("simulate, save restore and audio output survive scene switching")

    def assert_cook_manifest(self):
        cooked = self.work / "cooked"
        self.run("cook", "--project", self.project, "--target", "windows", "--output", cooked)
        manifest = json.loads((cooked / "cook-manifest.json").read_text(encoding="utf-8"))
        files = {entry["path"] for entry in manifest["files"]}
        joined = "\n".join(sorted(files))
        require("modules/value_room.luau" in files, "cook manifest omitted room module")
        require("assets/only_room.sprite.json" in files and "assets/room_ping.music.json" in files,
                "cook manifest omitted room-only asset recipes")
        require(any("only_room" in path for path in files), "cook manifest omitted room-only asset files")
        scene_assets = json.dumps(manifest.get("scene_assets", {}), sort_keys=True)
        require("only_room" in scene_assets and "room_ping" in scene_assets and "shared" in scene_assets,
                "cook manifest scene_assets omitted room-only or overridden assets: " + scene_assets + joined)
        self.run("validate", "--project", cooked)
        cooked_state = self.run("simulate", "--project", cooked, "--steps", "5",
                                "--input", self.work / "go_room.input.json")["state"]
        require(cooked_state["scene"] == "room" and cooked_state["state"]["number"] == 2,
                "cooked project did not preserve room graph replacement")
        self.checks.append("cook manifest records room scene assets and files")

    def assert_revision_changes(self, before):
        module = self.project / "modules/value_room.luau"
        saved = json.loads(self.initial_save.read_text(encoding="utf-8"))
        before_fingerprint = saved["save_compatibility"]["structural_fingerprint"]
        module.write_text("return {n=3}\n", encoding="utf-8")
        after = self.run("inspect", "--project", self.project)["revision"]
        require(after != before, "source revision did not change after room module edit")
        compatibility = self.run("save-check", "--project", self.project, "--file", self.initial_save)
        require(compatibility["status"] == "migration_required",
                "room module gameplay change did not require save migration")
        require(compatibility["structural_fingerprint"] != before_fingerprint,
                "room module gameplay change did not alter save structural fingerprint")
        module.write_text("return {n=2}\n", encoding="utf-8")
        self.checks.append("source revision and save structure include scene-local module bytes")

    def assert_rejections(self):
        module = self.project / "scenes/room.json"
        original = json.loads(module.read_text(encoding="utf-8"))
        bad = dict(original)
        bad["modules"] = {"value": 7}
        write_json(module, bad)
        self.run("check", "--project", self.project, ok=False)
        write_json(module, original)

        room_module = self.project / "modules/value_room.luau"
        original_module = room_module.read_text(encoding="utf-8")
        room_module.write_text("local n: number='bad'\nreturn {n=n}\n", encoding="utf-8")
        self.run("check", "--project", self.project, ok=False)
        room_module.write_text(original_module, encoding="utf-8")

        too_many = self.work / "too-many-assets"
        self.copy_fixture(too_many)
        scene = json.loads((too_many / "scenes/room.json").read_text(encoding="utf-8"))
        scene["assets"] = {
            f"asset_{index}": {"type": "sprite", "path": "assets/only_room.sprite.json"}
            for index in range(257)
        }
        write_json(too_many / "scenes/room.json", scene)
        self.run("validate", "--project", too_many, ok=False)

        escaping = self.work / "escaping-path"
        self.copy_fixture(escaping)
        scene = json.loads((escaping / "scenes/room.json").read_text(encoding="utf-8"))
        scene["assets"]["escape"] = {"type": "sprite", "path": "../escape.sprite.json"}
        write_json(escaping / "scenes/room.json", scene)
        self.run("validate", "--project", escaping, ok=False)
        self.checks.append("invalid room registry, Luau type error, asset count and parent traversal are rejected")

    def run_all(self):
        self.create_fixture()
        revision = self.assert_initial_commands()
        self.assert_scene_runtime()
        self.assert_cook_manifest()
        self.assert_revision_changes(revision)
        self.assert_rejections()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, default=ROOT / "build" / "ivy-langmake.exe")
    parser.add_argument("--work", type=Path, default=ROOT / ".cache" / "scene-catalog-tests")
    args = parser.parse_args()

    run_root = args.work / uuid.uuid4().hex
    run_root.mkdir(parents=True, exist_ok=False)
    harness = Harness(args.engine.resolve(), run_root.resolve())
    try:
        harness.run_all()
    except Exception as error:
        report = {"ok": False, "error": str(error), "engine": str(harness.engine), "work": str(run_root)}
        (run_root / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report, indent=2))
        return 1

    report = {"ok": True, "checks": harness.checks, "engine": str(harness.engine), "work": str(run_root)}
    (run_root / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
