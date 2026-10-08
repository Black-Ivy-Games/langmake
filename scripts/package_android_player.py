"""Package already-built Android runtimes; all inputs and output stay in the workspace."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('langmake_android', ROOT / 'scripts/android/android.py')
android = importlib.util.module_from_spec(spec)
spec.loader.exec_module(android)


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', action='append', required=True,
                        help='CONFIGURATION:ABI:workspace-relative-build-directory (repeat per variant)')
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    output = android.scoped(args.output)
    if output.exists():
        raise ValueError('Use a new Android player output directory.')
    lock = json.loads((ROOT / 'dependencies.lock.json').read_text())
    strip = android.SDK / 'ndk' / android.LOCK['ndk'] / 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe'
    if not strip.is_file():
        raise ValueError('The pinned workspace NDK strip tool is required to prepare the distribution.')
    variants = []
    seen = set()
    resources = {}
    # Validate every input before creating the package.
    for value in args.native:
        configuration, abi, relative = value.split(':', 2)
        if configuration not in ('debug', 'release') or abi not in ('arm64-v8a', 'x86_64'):
            raise ValueError('Unsupported Android player configuration or ABI.')
        if (configuration, abi) in seen:
            raise ValueError('Duplicate Android player variant.')
        seen.add((configuration, abi))
        native = android.scoped(relative)
        android.native_cache_configuration(native, abi, configuration)
        cache = (native / 'CMakeCache.txt').read_text(encoding='utf-8')
        version = re.search(r'^CMAKE_PROJECT_VERSION:[^=]*=(.+)$', cache, re.MULTILINE)
        if not version or version.group(1).strip() != lock['release']:
            raise ValueError('Native CMake version must match the release inventory: ' + str(native))
        library = native / 'libmain.so'
        android.verify_native_library(library, abi)
        for name in ['shaders/vs_cubes.bin', 'shaders/fs_cubes.bin', 'fonts/LatoLatin-Regular.ttf']:
            file = native / name
            if not file.is_file():
                raise ValueError('Missing runtime resource: ' + str(file))
            if name in resources and android.digest(file) != android.digest(resources[name]):
                raise ValueError('Android variants have different runtime resources: ' + name)
            resources[name] = file
        variants.append((configuration, abi, library))
    output.mkdir(parents=True)
    for name, source in resources.items():
        target = output / 'runtime' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    for configuration, abi, source in variants:
        folder = output / abi / configuration
        folder.mkdir(parents=True)
        library = folder / 'libmain.so'
        shutil.copyfile(source, library)
        subprocess.run([str(strip), '--strip-unneeded', str(library)], check=True, env=android.env())
        validation = android.verify_native_library(library, abi)
        write_json(folder / 'player.json', {
            'format_version': 1, 'engine_version': lock['release'],
            'audio_feature_revision': 1 if b'langmake_recorded_audio_v1' in source.read_bytes() else 0,
            'abi': abi, 'configuration': configuration, 'development': configuration == 'debug',
            'sha256': validation['sha256'], 'source_library_sha256': android.digest(source),
            'page_alignment': validation['page_alignment'], 'symbols': 'stripped; retained in source build',
            'min_sdk': android.LOCK['min_sdk'], 'renderer': 'OpenGL ES 3',
        })
    write_json(output / 'manifest.json', {
        'format_version': 1, 'engine_version': lock['release'],
        'note': 'Build inventory; device qualification is recorded separately in ANDROID-QUALIFICATION.md.',
        'files': [{'path': p.relative_to(output).as_posix(), 'bytes': p.stat().st_size,
                   'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
                  for p in sorted(output.rglob('*')) if p.is_file()],
    })
    print(json.dumps({'ok': True, 'output': str(output), 'variants': len(variants)}, indent=2))


if __name__ == '__main__':
    main()
