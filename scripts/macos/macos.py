#!/usr/bin/env python3
"""Package a cooked game and the native macOS player, without Studio."""
import argparse
import hashlib
import json
import plistlib
import re
import shutil
import subprocess
import sys
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def scoped(value):
    path = Path(value).resolve()
    path.relative_to(ROOT)
    return path


def run(args):
    result = subprocess.run([str(x) for x in args], capture_output=True, text=True)
    if result.stderr:
        print(result.stderr, file=sys.stderr)
    if result.returncode:
        raise ValueError('Command failed: ' + str(args[0]) + '\n' + result.stdout[-6000:] + result.stderr[-2000:])
    return result.stdout


def package(args):
    if sys.platform != 'darwin':
        raise ValueError('Packaging requires the authorized Mac with a native player build.')
    project, build, output = scoped(args.project), scoped(args.host_build), scoped(args.output)
    if output.exists():
        raise ValueError('Output must be a new directory; existing packages are never overwritten.')
    for name in ['langmake', 'langmake_macos_player']:
        if not (build / name).is_file():
            raise ValueError('Missing native executable: ' + str(build / name))
    work = scoped(output.with_name(output.name + '.stage-' + uuid.uuid4().hex))
    work.mkdir(parents=True)
    try:
        cooked = work / 'cooked'
        cook = json.loads(run([build / 'langmake', 'cook', '--project', project, '--profile', args.profile,
                               '--target', 'macos', '--output', cooked]))
        app = dict(cook['application'])
        app.update(app.get('platforms', {}).get('macos', {}))
        identifier = args.bundle_id or app.get('id', '')
        if not re.fullmatch(r'[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+', identifier):
            raise ValueError('A reverse-DNS bundle identifier is required.')
        label = app.get('label', 'Game')
        if not label or any(c in label for c in '/\\:') or label in ['.', '..']:
            raise ValueError('Invalid app label for a bundle filename.')
        bundle = work / (label + '.app')
        resources = bundle / 'Contents/Resources'
        binaries = bundle / 'Contents/MacOS'
        resources.mkdir(parents=True)
        binaries.mkdir()
        shutil.move(str(cooked), resources / 'game')
        shutil.copy2(build / 'langmake_macos_player', binaries / 'GamePlayer')
        source = ROOT / '.deps/source'
        (resources / 'shaders').mkdir()
        for name in ['vs_cubes', 'fs_cubes']:
            shutil.copy2(source / ('bgfx.cmake/bgfx/examples/runtime/shaders/metal/' + name + '.bin'), resources / 'shaders' / (name + '.bin'))
        (resources / 'fonts').mkdir()
        shutil.copy2(source / 'RmlUi/Samples/assets/LatoLatin-Regular.ttf', resources / 'fonts/LatoLatin-Regular.ttf')
        shutil.copytree(ROOT / 'licenses', resources / 'licenses')
        for name in ['LICENSE', 'THIRD_PARTY_NOTICES.md']:
            shutil.copy2(ROOT / name, resources / name)
        info = {'CFBundleIdentifier': identifier, 'CFBundleName': label, 'CFBundleDisplayName': label,
                'CFBundleExecutable': 'GamePlayer', 'CFBundlePackageType': 'APPL',
                'CFBundleShortVersionString': str(app.get('version', '1.0.0')),
                'CFBundleVersion': str(app.get('build', 1)), 'LSMinimumSystemVersion': '14.0',
                'NSHighResolutionCapable': True, 'NSPrincipalClass': 'NSApplication'}
        icon = app.get('icon')
        if icon:
            image = (resources / 'game' / icon).resolve()
            image.relative_to(resources / 'game')
            iconset = work / 'AppIcon.iconset'
            iconset.mkdir()
            for size in [16, 32, 128, 256, 512]:
                for scale in [1, 2]:
                    filename = f'icon_{size}x{size}' + ('@2x' if scale == 2 else '') + '.png'
                    run(['sips', '-z', str(size * scale), str(size * scale), image, '--out', iconset / filename])
            run(['iconutil', '-c', 'icns', iconset, '-o', resources / 'AppIcon.icns'])
            info['CFBundleIconFile'] = 'AppIcon'
            shutil.rmtree(iconset)
        with (bundle / 'Contents/Info.plist').open('wb') as stream:
            plistlib.dump(info, stream)
        # Local Mac testing does not require a distribution certificate. This is
        # explicitly ad-hoc signed, not notarized or ready for public distribution.
        run(['codesign', '--force', '--sign', '-', bundle])
        run(['codesign', '--verify', '--deep', '--strict', bundle])
        report = {'ok': True, 'platform': 'macos', 'configuration': 'release', 'profile': args.profile,
                  'bundle_id': identifier, 'app': str(output / bundle.name),
                  'source_revision': cook['source_revision'], 'packaged_revision': cook['packaged_revision'],
                  'signing': 'ad-hoc-local-testing', 'notarized': False, 'device_qualified': False,
                  'architecture': run(['lipo', '-archs', binaries / 'GamePlayer']).strip(), 'files': []}
        for path in sorted(bundle.rglob('*')):
            if path.is_file():
                report['files'].append({'path': path.relative_to(bundle).as_posix(), 'bytes': path.stat().st_size,
                                        'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
        (work / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
        work.rename(output)
        return {key: value for key, value in report.items() if key != 'files'}
    except Exception:
        shutil.rmtree(work)
        raise


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', required=True)
    parser.add_argument('--profile', default='desktop')
    parser.add_argument('--host-build', required=True, help='Directory containing the two Release executables')
    parser.add_argument('--output', required=True)
    parser.add_argument('--bundle-id')
    try:
        print(json.dumps(package(parser.parse_args()), indent=2))
    except Exception as error:
        print(json.dumps({'ok': False, 'error': str(error)}, indent=2))
        sys.exit(1)
