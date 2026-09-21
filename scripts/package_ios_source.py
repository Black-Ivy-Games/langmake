"""Create a reviewable iOS source handoff without private projects or host caches."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
TOP_FILES = ("AGENTS.md", "README.md", "CMakeLists.txt", "LICENSE",
             "THIRD_PARTY_NOTICES.md", "dependencies.lock.json", ".gitignore")
SOURCE_TREES = ("src", "include", "shaders", "platform", "scripts", "tests",
                "docs", "prompts", "licenses")
TEMPLATES = ("2d", "3d", "rockets-2d", "rockets-3d", "adventure", "strategy", "shared-2d", "workflow-2d")
EXCLUDED = {".cache", ".deps", ".langmake", ".git", "__pycache__", "node_modules", ".data"}
STUDIO_FILES = ("package.json", "package-lock.json", "main.cjs", "preload.cjs",
                "README.md", "LICENSE", "THIRD_PARTY_NOTICES.md", "launch.ps1", ".gitignore")


def within(path, parent):
    return path == parent or parent in path.parents


def collect(root, include_studio=False):
    paths = [root / p for p in TOP_FILES]
    trees = list(SOURCE_TREES) + ["examples/" + t for t in TEMPLATES]
    if include_studio:
        paths.extend(root / "studio" / p for p in STUDIO_FILES)
        trees.extend("studio/" + p for p in ("core", "providers", "renderer", "scripts", "test"))
    for tree in trees:
        paths.extend(p for p in (root / tree).rglob("*") if p.is_file())
    files = {}
    for path in paths:
        rel = path.relative_to(root)
        if any(part in EXCLUDED for part in rel.parts) or path.suffix in (".pyc", ".log"):
            continue
        if path.is_symlink() or not within(path.resolve(), root):
            raise ValueError("Source package refuses linked/escaped files: " + str(rel))
        if not path.is_file():
            raise ValueError("Required source package file missing: " + str(rel))
        files[rel.as_posix()] = path.read_bytes()
    return files


def package(output, include_studio=False, staged_directory=None):
    output = output.resolve()
    if not within(output, ROOT) or output == ROOT or output.exists():
        raise ValueError("Output must be a new ZIP inside the Langmake workspace")
    if output.suffix.lower() != ".zip":
        raise ValueError("Output must end in .zip")
    files = collect(ROOT, include_studio)
    if staged_directory is not None:
        staged = staged_directory.resolve()
        if not within(staged, ROOT) or not staged.is_dir():
            raise ValueError("Staged examples must be inside the Langmake workspace")
        specification = importlib.util.spec_from_file_location("langmake_ios_package_tools", ROOT / "scripts/ios/ios.py")
        ios = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(ios)
        for template in TEMPLATES:
            folder = staged / template
            ios.verify_stage(folder)
            for file in folder.rglob("*"):
                if file.is_file():
                    files["ios-examples/" + template + "/" + file.relative_to(folder).as_posix()] = file.read_bytes()
    for required in ("src/ios_main.mm", "scripts/ios/ios.py", "scripts/ios/README.md",
                     "docs/23-ios-agent-guide.md", "docs/24-ios-qualification.md"):
        if required not in files:
            raise ValueError("iOS handoff source is incomplete: " + required)
    manifest = {"format_version": 1, "engine_version": "0.5.0",
                "kind": "ios-source-preview", "ios_qualified": False,
                "studio_source_included": include_studio,
                "staged_examples_included": staged_directory is not None,
                "dependencies": "Pinned sources are fetched inside the extracted workspace by CMake; Xcode is not bundled",
                "entry_guide": "docs/23-ios-agent-guide.md", "files": []}
    for name, data in sorted(files.items()):
        manifest["files"].append({"path": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    files["ios-source-manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode("utf-8")
    output.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive creation preserves existing packages even if another writer races us.
    with output.open("xb") as stream:
        with zipfile.ZipFile(stream, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for name, data in sorted(files.items()):
                info = zipfile.ZipInfo(name, date_time=(2026, 9, 19, 0, 0, 0))
                info.external_attr = 0o100644 << 16
                archive.writestr(info, data, compress_type=zipfile.ZIP_DEFLATED)
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None or set(archive.namelist()) != set(files):
            raise ValueError("Source archive integrity verification failed")
        for item in manifest["files"]:
            if hashlib.sha256(archive.read(item["path"])).hexdigest() != item["sha256"]:
                raise ValueError("Source archive content mismatch: " + item["path"])
    return {"ok": True, "output": str(output), "files": len(files), "ios_qualified": False,
            "sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
            "next": "Extract in the authorized Mac workspace; follow docs/23-ios-agent-guide.md"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--include-studio", action="store_true")
    parser.add_argument("--staged-directory", type=Path, help="Include all eight verified stages as ios-examples/")
    args = parser.parse_args()
    try:
        print(json.dumps(package(args.output, args.include_studio, args.staged_directory), indent=2))
    except (ValueError, OSError, zipfile.BadZipFile) as exc:
        print(json.dumps({"ok": False, "error": str(exc)}))
        sys.exit(1)


if __name__ == "__main__":
    main()
