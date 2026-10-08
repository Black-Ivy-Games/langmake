"""Workspace-local Android build/deployment tools. Run through android.ps1 or Python."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import struct
import sys
import tempfile
import time
import zipfile
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import mobile_assets

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / '.deps/android'
CACHE = ROOT / '.cache/android'
LOCK = json.loads((Path(__file__).parent / 'toolchain.lock.json').read_text())
SDK = TOOLS / 'sdk'
JDK = TOOLS / 'jdk' / LOCK['jdk']['archive_root']
GRADLE = TOOLS / 'gradle' / LOCK['gradle']['archive_root']
APP_ID = re.compile(r'[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z][A-Za-z0-9_]*)+')
SERIAL = re.compile(r'[A-Za-z0-9_.:\-]+')


def scoped(path):
    value = Path(path)
    value = (value if value.is_absolute() else ROOT / value).resolve()
    try:
        value.relative_to(ROOT)
    except ValueError:
        raise ValueError('Path must stay inside the Langmake workspace: ' + str(value))
    return value


def selected_path(path):
    """An explicit application input/output selected independently of the engine installation."""
    value = Path(path)
    return (value if value.is_absolute() else ROOT / value).resolve()


def env():
    e = os.environ.copy()
    paths = {'ANDROID_HOME': SDK, 'ANDROID_SDK_ROOT': SDK,
             'ANDROID_USER_HOME': CACHE / 'user/.android', 'ANDROID_SDK_HOME': CACHE / 'user',
             'ANDROID_AVD_HOME': CACHE / 'avd', 'GRADLE_USER_HOME': CACHE / 'gradle',
             'TEMP': CACHE / 'tmp', 'TMP': CACHE / 'tmp', 'TMPDIR': CACHE / 'tmp',
             'PYTHONPYCACHEPREFIX': CACHE / 'python'}
    for key, path in paths.items():
        path.mkdir(parents=True, exist_ok=True)
        e[key] = str(path)
    e['ADB_VENDOR_KEYS'] = str(CACHE / 'user/.android/adbkey')
    e['ANDROID_ADB_SERVER_PORT'] = '5038'
    e['JAVA_HOME'] = str(JDK)
    e['JAVA_TOOL_OPTIONS'] = '-Duser.home="' + str(CACHE / 'user') + '"'
    e['PATH'] = str(JDK / 'bin') + os.pathsep + e.get('PATH', '')
    return e


def run(command, capture=False, input_data=None, timeout=None, check=True):
    command = [str(x) for x in command]
    # Execute Java launchers directly. cmd.exe batch quoting must never interpret game names/paths.
    launcher = Path(command[0]).name.lower()
    if launcher == 'gradle.bat':
        command = [str(JDK / 'bin/java.exe'), '-classpath', str(GRADLE / 'lib' / ('gradle-launcher-' + LOCK['gradle']['version'] + '.jar')),
                   'org.gradle.launcher.GradleMain'] + command[1:]
    elif launcher == 'apksigner.bat':
        command = [str(JDK / 'bin/java.exe'), '-jar', str(SDK / 'build-tools' / LOCK['build_tools'] / 'lib/apksigner.jar')] + command[1:]
    elif launcher == 'avdmanager.bat':
        command = [str(JDK / 'bin/java.exe'), '-Dcom.android.sdkmanager.toolsdir=' + str(SDK / 'cmdline-tools/19.0'),
                   '-classpath', str(SDK / 'cmdline-tools/19.0/lib/avdmanager-classpath.jar'),
                   'com.android.sdklib.tool.AvdManagerCli'] + command[1:]
    print('+ ' + subprocess.list2cmdline([str(x) for x in command]), file=sys.stderr)
    p = subprocess.run(command, cwd=ROOT, env=env(), input=input_data,
                       stdout=subprocess.PIPE if capture else sys.stderr,
                       stderr=subprocess.PIPE if capture else sys.stderr, timeout=timeout)
    if check and p.returncode:
        raise RuntimeError('Command failed (%s): %s' % (p.returncode, p.stderr.decode('utf-8', 'replace') if capture else command[0]))
    return p


def digest(path, algorithm='sha256'):
    h = hashlib.new(algorithm)
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def download(spec):
    folder = CACHE / 'downloads'
    folder.mkdir(parents=True, exist_ok=True)
    name = spec['url'].rsplit('/', 1)[-1]
    target = scoped(folder / name)
    if target.is_file() and digest(target, spec['hash_algorithm']) == spec['hash']:
        return target
    partial = target.with_suffix(target.suffix + '.partial')
    run(['curl.exe', '--fail', '--location', '--retry', '3', '--output', partial, spec['url']])
    if 'size' in spec and partial.stat().st_size != spec['size']:
        raise ValueError('Downloaded size disagrees with the pinned toolchain: ' + name)
    if digest(partial, spec['hash_algorithm']) != spec['hash']:
        raise ValueError('Downloaded hash disagrees with the pinned toolchain: ' + name)
    partial.replace(target)
    return target


def extract(archive, destination, strip_root=None):
    destination = scoped(destination)
    destination.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as z:
        for entry in z.infolist():
            name = PurePosixPath(entry.filename)
            original = entry.orig_filename
            if name.is_absolute() or '..' in name.parts or '\\' in original or ':' in original or '\0' in original:
                raise ValueError('Unsafe archive member: ' + entry.filename)
            if ((entry.external_attr >> 16) & 0o170000) == 0o120000:
                raise ValueError('Archive symlink is unsupported: ' + entry.filename)
            parts = name.parts
            if strip_root:
                if not parts or parts[0] != strip_root:
                    raise ValueError('Unexpected archive root: ' + entry.filename)
                parts = parts[1:]
            if not parts:
                continue
            output = scoped(destination.joinpath(*parts))
            if entry.is_dir():
                output.mkdir(parents=True, exist_ok=True)
            else:
                output.parent.mkdir(parents=True, exist_ok=True)
                with z.open(entry) as src, output.open('wb') as dst:
                    shutil.copyfileobj(src, dst, 1024 * 1024)


def install_archive(spec, destination, strip_root=None):
    marker = scoped(destination) / '.langmake-install.json'
    if marker.is_file() and json.loads(marker.read_text()).get('hash') == spec['hash']:
        return
    archive = download(spec)
    extract(archive, destination, strip_root)
    marker.write_text(json.dumps({'url': spec['url'], 'hash': spec['hash'], 'archive_sha256': digest(archive)}, indent=2) + '\n')


def write_sdk_metadata(package):
    # Unlike legacy platform/build-tools archives, these packages need localPackage metadata.
    if not (package['id'].startswith(('ndk;', 'cmdline-tools;', 'system-images;')) or package['id'] == 'emulator'):
        return
    common = 'http://schemas.android.com/repository/android/common/02'
    generic = 'http://schemas.android.com/repository/android/generic/02'
    xsi = 'http://www.w3.org/2001/XMLSchema-instance'
    sysimg = 'http://schemas.android.com/sdk/android/repo/sys-img2/03'
    ET.register_namespace('repo', common)
    ET.register_namespace('xsi', xsi)
    attributes = {'xmlns:generic': generic}
    if package['id'].startswith('system-images;'):
        attributes['xmlns:sysimg'] = sysimg
    root = ET.Element('{' + common + '}repository', attributes)
    license = ET.SubElement(root, 'license', {'id': 'android-sdk-license', 'type': 'text'})
    license.text = (Path(__file__).parent / 'android-sdk-license.txt').read_text(encoding='utf-8')
    local = ET.SubElement(root, 'localPackage', {'path': package['id'], 'obsolete': 'false'})
    details = ET.SubElement(local, 'type-details', {'{' + xsi + '}type': 'sysimg:sysImgDetailsType' if package['id'].startswith('system-images;') else 'generic:genericDetailsType'})
    if package['id'].startswith('system-images;'):
        _, platform, tag_id, abi = package['id'].split(';')
        ET.SubElement(details, 'api-level').text = platform.split('-')[1]
        tag = ET.SubElement(details, 'tag')
        ET.SubElement(tag, 'id').text = tag_id
        ET.SubElement(tag, 'display').text = 'Default Android System Image'
        ET.SubElement(details, 'abi').text = abi
    revision = ET.SubElement(local, 'revision')
    for key, value in zip(['major', 'minor', 'micro'], package['revision'].split('.')):
        ET.SubElement(revision, key).text = value
    ET.SubElement(local, 'display-name').text = package['id']
    ET.SubElement(local, 'uses-license', {'ref': 'android-sdk-license'})
    output = SDK / package['destination'] / 'package.xml'
    data = ET.tostring(root, encoding='utf-8', xml_declaration=True)
    if output.is_file() and output.read_bytes() == data:
        return
    temporary = output.with_suffix('.langmake-new.xml')
    temporary.write_bytes(data)
    temporary.replace(output)


def player_toolchain(args):
    return bool(getattr(args, 'player_only', False) or
                ((ROOT / 'android-player').is_dir() and not (ROOT / 'CMakeLists.txt').is_file()))


def bootstrap(args):
    install_archive(LOCK['jdk'], TOOLS / 'jdk')
    install_archive(LOCK['gradle'], TOOLS / 'gradle')
    if not args.accept_sdk_licenses:
        return {'ok': False, 'host_tools_ready': True, 'code': 'sdk_license_acceptance_required',
                'terms': str(Path(__file__).parent / 'android-sdk-license.txt'),
                'terms_url': LOCK['sdk_license_url'],
                'next': 'Read the SDK terms; if accepted, run bootstrap --accept-sdk-licenses.'}
    # This flag is an explicit human acceptance, never supplied implicitly by build/doctor.
    # Match com.android.repository.api.License: SHA-1 of the exact UTF-8 license value, including final newline.
    terms = (Path(__file__).parent / 'android-sdk-license.txt').read_text(encoding='utf-8')
    license_dir = SDK / 'licenses'
    license_dir.mkdir(parents=True, exist_ok=True)
    (license_dir / 'android-sdk-license').write_text(hashlib.sha1(terms.encode('utf-8')).hexdigest() + '\n')
    (TOOLS / 'sdk-license-acceptance.json').write_text(json.dumps({
        'accepted_via': 'explicit --accept-sdk-licenses', 'terms_sha256': hashlib.sha256(terms.encode()).hexdigest(),
        'terms_url': LOCK['sdk_license_url'], 'unix_time': int(time.time())}, indent=2) + '\n')
    for package in LOCK['sdk_packages']:
        if player_toolchain(args) and package['id'].startswith('ndk;'):
            continue
        install_archive(package, SDK / package['destination'], package['archive_root'])
        write_sdk_metadata(package)
    if args.emulator:
        install_emulator()
    return doctor(args)


def doctor(args):
    checks = []
    for name, file in [('jdk', JDK / 'bin/java.exe'), ('gradle', GRADLE / 'bin/gradle.bat'),
                       ('ndk', SDK / 'ndk' / LOCK['ndk'] / 'build/cmake/android.toolchain.cmake'),
                       ('platform', SDK / 'platforms/android-36/android.jar'),
                       ('build_tools', SDK / 'build-tools' / LOCK['build_tools'] / 'aapt2.exe'),
                       ('adb', SDK / 'platform-tools/adb.exe')]:
        checks.append({'name': name, 'path': str(file), 'available': file.is_file()})
    for name in ['cmake', 'ninja', 'git']:
        checks.append({'name': name, 'path': shutil.which(name), 'available': shutil.which(name) is not None})
    if player_toolchain(args):
        checks = [item for item in checks if item['name'] not in ['ndk', 'cmake', 'ninja', 'git']]
    return {'ok': all(c['available'] for c in checks), 'host': 'windows-x64', 'checks': checks,
            'mode': 'prebuilt-player' if player_toolchain(args) else 'source-build',
            'toolchain': {'agp': LOCK['agp'], 'ndk': LOCK['ndk'], 'sdk': LOCK['compile_sdk'],
                          'gradle': LOCK['gradle']['version'], 'jdk': LOCK['jdk']['version']},
            'device_qualification': 'not checked', 'sdk_licenses_recorded': (TOOLS / 'sdk-license-acceptance.json').is_file()}


def valid_app(value):
    if not APP_ID.fullmatch(value):
        raise ValueError('Application ID must be a dotted Java identifier.')
    return value


def copy_tree(source, destination):
    source, destination = scoped(source), scoped(destination)
    for file in source.rglob('*'):
        if file.is_file():
            scoped(file)
            target = scoped(destination / file.relative_to(source))
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(file, target)


def stage_bundle(cooked, runtime, assets, application=None, icon=None):
    cooked, runtime, assets = scoped(cooked), scoped(runtime), scoped(assets)
    cook = json.loads((cooked / 'cook-manifest.json').read_text())
    if cook.get('format_version') != 1 or cook.get('target') != 'android':
        raise ValueError('A version-1 Android cook bundle is required.')
    source_files = [item['path'] for item in cook['files']]
    source_files.append('cook-manifest.json')
    entries = []
    for relative in sorted(set(source_files)):
        name = PurePosixPath(relative)
        if name.is_absolute() or '..' in name.parts or '\\' in relative or ':' in relative:
            raise ValueError('Unsafe cooked source path: ' + relative)
        source = scoped(cooked / relative)
        source.relative_to(cooked)
        output = scoped(assets / 'game' / relative)
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, output)
        entries.append({'path': 'game/' + relative, 'bytes': output.stat().st_size, 'sha256': digest(output)})
    for directory in ['shaders', 'fonts']:
        folder = runtime / directory
        if not folder.is_dir():
            raise ValueError('Missing Android runtime directory: ' + str(folder))
        for source in sorted(folder.rglob('*')):
            if not source.is_file():
                continue
            scoped(source)
            relative = 'runtime/' + source.relative_to(runtime).as_posix()
            output = scoped(assets / relative)
            output.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, output)
            entries.append({'path': relative, 'bytes': output.stat().st_size, 'sha256': digest(output)})
    notices = [ROOT / 'LICENSE', ROOT / 'THIRD_PARTY_NOTICES.md', ROOT / 'dependencies.lock.json']
    license_directory = ROOT / 'licenses'
    if not license_directory.is_dir() or any(not file.is_file() for file in notices):
        raise ValueError('Engine license inventory is missing; cannot package the Android player.')
    notices += sorted(file for file in license_directory.rglob('*') if file.is_file())
    for source in notices:
        scoped(source)
        suffix = 'third-party/' + source.relative_to(license_directory).as_posix() if source.is_relative_to(license_directory) else source.name
        relative = 'runtime/licenses/' + suffix
        output = scoped(assets / relative)
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, output)
        entries.append({'path': relative, 'bytes': output.stat().st_size, 'sha256': digest(output)})
    project = json.loads((cooked / cook['project_manifest']).read_text(encoding='utf-8-sig'))
    identity = str(project.get('project_id', project.get('title', 'game')))
    manifest = {'format_version': 1, 'source_revision': cook['source_revision'],
                'packaged_revision': cook.get('packaged_revision', cook['source_revision']),
                'project_manifest': 'game/' + cook['project_manifest'],
                'storage_id': hashlib.sha256(identity.encode()).hexdigest()[:24], 'files': entries}
    if application is not None:
        manifest.update(application=application, icon=icon, profile=project.get('default_profile'))
    manifest['content_id'] = hashlib.sha256(json.dumps(manifest, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    (assets / 'install-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


def build_inputs(args):
    player = scoped(args.player_dir) if args.player_dir else ROOT / 'android-player'
    portable = player.is_dir()
    if args.host_build:
        host = scoped(args.host_build)
    else:
        candidates = [ROOT, ROOT / 'build', ROOT / '.cache/build-0.4.1', ROOT / '.cache/build-0.4']
        host = next((candidate for candidate in candidates if (candidate / 'langmake.exe').is_file()), ROOT / 'build')
    if args.player_dir and not portable:
        raise ValueError('Explicit --player-dir does not exist; no native build fallback was attempted.')
    if not portable and not (ROOT / 'CMakeLists.txt').is_file():
        raise ValueError('This package does not include an Android player. Use an Android-enabled player package or the Langmake source repository; installing the SDK alone cannot add the missing native library.')
    return player, portable, host


def host_capabilities(host):
    executable = host / 'langmake.exe'
    if not executable.is_file():
        raise ValueError('Host engine is missing: ' + str(executable) + '. Build the current Langmake release or supply --host-build with its cook-capable executable.')
    result = run([executable, 'capabilities'], capture=True, timeout=15, check=False)
    try:
        capabilities = json.loads(result.stdout)
    except (ValueError, UnicodeError):
        raise ValueError('Host engine did not return valid capabilities; choose the current engine with --host-build.')
    if result.returncode != 0 or not isinstance(capabilities, dict) or 'cook' not in capabilities.get('commands', []):
        version = capabilities.get('version', 'unknown') if isinstance(capabilities, dict) else 'unknown'
        raise ValueError('Selected host engine ' + str(executable) + ' (version ' + str(version) + ') does not support cook. Use Langmake 0.4 or newer; for this source checkout pass --host-build .cache/build-0.4.1.')
    return capabilities


def native_cache_configuration(directory, abi, configuration):
    cache = directory / 'CMakeCache.txt'
    if not cache.is_file():
        raise ValueError('Native CMake metadata is missing; rebuild instead of reusing this library.')
    values = {}
    for line in cache.read_text(errors='replace').splitlines():
        match = re.match(r'^([^#/:][^:]*):[^=]+=(.*)$', line)
        if match:
            values[match.group(1)] = match.group(2)
    if values.get('ANDROID_ABI') != abi:
        raise ValueError('Native CMake ABI does not match the requested Android package ABI.')
    if 'LM_ANDROID_DEVELOPMENT' not in values:
        raise ValueError('Native build does not record development-control configuration; rebuild it.')
    development = values['LM_ANDROID_DEVELOPMENT'].upper() in ['ON', 'TRUE', 'YES', '1']
    if development != (configuration == 'debug'):
        raise ValueError('Native development-control configuration does not match debug/release selection.')
    return development


def prebuilt_configuration(player, abi, configuration, library):
    metadata_file = library.parent / 'player.json'
    metadata = json.loads(metadata_file.read_text()) if metadata_file.is_file() else {'development': True}
    if not isinstance(metadata.get('development'), bool):
        raise ValueError('Prebuilt player metadata must declare a boolean development field.')
    if configuration == 'release':
        if metadata['development']:
            raise ValueError('This prebuilt player enables development controls; a release player is required for release export.')
        if metadata.get('abi') != abi or metadata.get('sha256') != digest(library):
            raise ValueError('Release player metadata must match the library ABI and SHA-256 hash.')
    elif not metadata['development']:
        raise ValueError('This prebuilt player disables development controls; select release or provide a debug player.')
    return metadata['development']


def prebuilt_library(player, abi, configuration):
    variant = player / abi / configuration
    # A variant directory is authoritative when present; never silently substitute another configuration.
    return (variant if variant.is_dir() else player / abi) / 'libmain.so'


def prebuilt_inputs(player, abi, configuration):
    library = prebuilt_library(player, abi, configuration)
    if not library.is_file():
        raise ValueError('This package does not include an Android player for ABI ' + abi)
    for relative in ['runtime/shaders/vs_cubes.bin', 'runtime/shaders/fs_cubes.bin', 'runtime/fonts/LatoLatin-Regular.ttf']:
        if not (player / relative).is_file():
            raise ValueError('Android player package is incomplete: missing ' + relative)
    java = ROOT / 'platform/android/app/src/main/java/org/libsdl/app/SDLActivity.java'
    fallback_java = ROOT / '.deps/source/SDL/android-project/app/src/main/java/org/libsdl/app/SDLActivity.java'
    if not java.is_file() and not fallback_java.is_file():
        raise ValueError('Android player package is incomplete: missing the SDL Java shell.')
    prebuilt_configuration(player, abi, configuration, library)
    verify_native_library(library, abi)


def verify_native_library(path, abi):
    data = Path(path).read_bytes()
    if len(data) < 64 or data[:6] != b'\x7fELF\x02\x01':
        raise ValueError('Android player must be a little-endian ELF64 shared library.')
    machine = struct.unpack_from('<H', data, 18)[0]
    if machine != {'arm64-v8a': 183, 'x86_64': 62}[abi]:
        raise ValueError('Native library ABI does not match requested APK ABI.')
    phoff = struct.unpack_from('<Q', data, 32)[0]
    phsize, phcount = struct.unpack_from('<HH', data, 54)
    if phsize < 56 or phcount > 1024 or phoff + phsize * phcount > len(data):
        raise ValueError('Native library has malformed ELF program headers.')
    loads = 0
    for index in range(phcount):
        kind, flags, offset, address, physical, file_size, memory_size, alignment = struct.unpack_from('<IIQQQQQQ', data, phoff + phsize * index)
        if kind != 1: continue
        loads += 1
        if alignment < 16384 or offset % 16384 != address % 16384:
            raise ValueError('Native library LOAD segments must support Android 16 KiB pages.')
    if not loads:
        raise ValueError('Native library has no ELF LOAD segments.')
    return {'abi': abi, 'elf_load_segments': loads, 'page_alignment': 16384, 'sha256': digest(path)}


def verify_packaged_library(apk, abi, destination):
    """Check the ELF actually shipped after Gradle's stripDebugSymbols step."""
    member = 'lib/' + abi + '/libmain.so'
    with zipfile.ZipFile(apk) as archive:
        entries = [entry for entry in archive.infolist() if entry.filename == member]
        if len(entries) != 1 or entries[0].file_size > 256 * 1024 * 1024:
            raise ValueError('APK must contain exactly one bounded native player for the selected ABI.')
        destination.parent.mkdir(parents=True, exist_ok=True)
        with archive.open(entries[0]) as source, destination.open('wb') as output:
            shutil.copyfileobj(source, output)
    return verify_native_library(destination, abi)


def native_cache_key(abi, configuration, shaderc):
    paths = [ROOT / 'CMakeLists.txt', ROOT / 'dependencies.lock.json']
    for directory in ['src', 'include', 'cmake', 'shaders']:
        paths += sorted(path for path in (ROOT / directory).rglob('*') if path.is_file())
    inputs = {'source': [(path.relative_to(ROOT).as_posix(), digest(path)) for path in paths if path.is_file()],
              'toolchain': LOCK, 'abi': abi, 'configuration': configuration, 'shaderc_sha256': digest(shaderc)}
    return hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()


def build(args):
    player, portable, host = build_inputs(args)
    if args.skip_native and args.configuration == 'release' and not portable:
        raise ValueError('--skip-native cannot produce a source release: rebuild with development controls disabled, or use a verified prebuilt release player.')
    # Reject incomplete player packages before recommending any toolchain downloads.
    if portable:
        prebuilt_inputs(player, args.abi, args.configuration)
    host_capabilities(host)
    args.player_only = portable
    report = doctor(args)
    if not report['ok']:
        raise ValueError('Android toolchain is incomplete; run doctor and bootstrap first. SDK licenses are never accepted by build.')
    abi = args.abi
    project = selected_path(args.project)
    if not project.exists():
        raise ValueError('Explicit game project does not exist: ' + str(project))
    build_id = time.strftime('%Y%m%d-%H%M%S') + '-' + str(time.time_ns() % 1000000)
    folder = scoped(CACHE / 'builds' / build_id)
    folder.mkdir(parents=True)
    cooked = folder / 'cooked'
    command = [host / 'langmake.exe', 'cook', '--project', project, '--output', cooked, '--target', 'android']
    if getattr(args, 'profile', None): command += ['--profile', args.profile]
    run(command)
    application = mobile_assets.metadata(json.loads((cooked / 'project.json').read_text(encoding='utf-8-sig')),
        'android', args.configuration, {'id': args.application_id, 'label': args.label, 'orientation': args.orientation,
        'version': getattr(args, 'version', None), 'build': getattr(args, 'build_number', None), 'icon': getattr(args, 'icon', None)})
    app_id = valid_app(application['id'])
    icon_source = mobile_assets.icon_path(cooked, application['icon']) if application.get('icon') else None
    if icon_source: mobile_assets.decode_png(icon_source)
    if portable:
        native = player / 'runtime'
        native_key = None
        libraries = [prebuilt_library(player, abi, args.configuration)]
        if not libraries[0].is_file():
            raise ValueError('Prebuilt Android player is missing for ABI ' + abi)
        prebuilt_configuration(player, abi, args.configuration, libraries[0])
    else:
        shaderc = scoped(args.shaderc) if args.shaderc else host / 'shaderc.exe'
        if not shaderc.is_file():
            matches = list(host.rglob('shaderc.exe'))
            if len(matches) != 1:
                raise ValueError('Specify --shaderc with the workspace host shaderc.exe path.')
            shaderc = matches[0]
        native_key = native_cache_key(abi, args.configuration, shaderc)
        native = scoped(args.native_build or (CACHE / 'native' / native_key))
        native.mkdir(parents=True, exist_ok=True)
        if not args.skip_native:
            run(['cmake', '-S', ROOT, '-B', native, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                 '-DCMAKE_TOOLCHAIN_FILE=' + str(SDK / 'ndk' / LOCK['ndk'] / 'build/cmake/android.toolchain.cmake'),
                 '-DANDROID_ABI=' + abi, '-DANDROID_PLATFORM=android-' + str(LOCK['min_sdk']),
                 '-DANDROID_STL=c++_static', '-DBUILD_TESTING=OFF',
                 '-DLM_ANDROID_DEVELOPMENT=' + ('ON' if args.configuration == 'debug' else 'OFF'),
                 '-DLM_HOST_SHADERC=' + str(shaderc)])
            run(['cmake', '--build', native, '--target', 'langmake', '--parallel', str(args.jobs)])
        native_cache_configuration(native, abi, args.configuration)
        libraries = list(native.rglob('libmain.so'))
    if len(libraries) != 1:
        raise ValueError('Native build must contain exactly one libmain.so.')
    requirements = json.loads((cooked / 'cook-manifest.json').read_text(encoding='utf-8'))
    if requirements.get('audio_feature_revision', 0) >= 1:
        if b'langmake_recorded_audio_v1' not in libraries[0].read_bytes():
            raise ValueError('This game requires expressive audio revision 1; rebuild the Android player or use a matching newer prebuilt player. Historical synthesis-only binaries cannot run this content.')
    native_validation = verify_native_library(libraries[0], abi)
    gradle_project = folder / 'gradle'
    copy_tree(ROOT / 'platform/android', gradle_project)
    packaged_java = gradle_project / 'app/src/main/java/org/libsdl/app/SDLActivity.java'
    if not packaged_java.is_file():
        java = ROOT / '.deps/source/SDL/android-project/app/src/main/java/org/libsdl/app'
        if not (java / 'SDLActivity.java').is_file():
            raise ValueError('Package is missing the SDL Java platform shell.')
        copy_tree(java, gradle_project / 'app/src/main/java/org/libsdl/app')
    assets = gradle_project / 'app/src/main/assets'
    assets.mkdir(parents=True, exist_ok=True)
    icon_report = mobile_assets.generate_icon(icon_source, gradle_project / 'app/src/main/res', 'android') if icon_source else None
    if icon_report:
        manifest_path = gradle_project / 'app/src/main/AndroidManifest.xml'
        ET.register_namespace('android', 'http://schemas.android.com/apk/res/android')
        document = ET.parse(manifest_path)
        document.getroot().find('application').set('{http://schemas.android.com/apk/res/android}icon', '@mipmap/ic_launcher')
        document.write(manifest_path, encoding='utf-8', xml_declaration=True)
    manifest = stage_bundle(cooked, native, assets, application, icon_report)
    jni = gradle_project / 'app/src/main/jniLibs' / abi
    jni.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(libraries[0], jni / 'libmain.so')
    # SDK location and debug signing material are project/workspace-local.
    (gradle_project / 'local.properties').write_text('sdk.dir=' + SDK.as_posix() + '\n')
    key = TOOLS / 'debug.keystore'
    if not key.exists():
        run([JDK / 'bin/keytool.exe', '-genkeypair', '-keystore', key, '-storepass', 'android', '-alias', 'androiddebugkey',
             '-keypass', 'android', '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000',
             '-dname', 'CN=Langmake Development,O=Langmake,C=US', '-noprompt'])
    gradle_command = [GRADLE / 'bin/gradle.bat', '--no-daemon', '--console=plain', '-p', gradle_project,
                      '-PlangmakeApplicationId=' + app_id, '-PlangmakeLabel=' + application['label'],
                      '-PlangmakeOrientation=' + application['orientation'],
                      '-PlangmakeVersion=' + application['version'], '-PlangmakeBuildNumber=' + str(application['build']),
                      '-PlangmakeDebugKeystore=' + key.as_posix()]
    signed = args.configuration == 'debug'
    if args.keystore:
        if args.configuration != 'release' or not args.key_alias or not args.store_password_env:
            raise ValueError('Release signing requires --configuration release, --key-alias and --store-password-env.')
        key_password_env = args.key_password_env or args.store_password_env
        for variable in [args.store_password_env, key_password_env]:
            if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', variable) or not os.environ.get(variable):
                raise ValueError('Signing password environment variable is missing or invalid: ' + variable)
        release_key = selected_path(args.keystore)
        if not release_key.is_file():
            raise ValueError('Release keystore does not exist.')
        gradle_command += ['-PlangmakeReleaseKeystore=' + release_key.as_posix(), '-PlangmakeKeyAlias=' + args.key_alias,
                           '-PlangmakeStorePasswordEnv=' + args.store_password_env,
                           '-PlangmakeKeyPasswordEnv=' + key_password_env]
        signed = True
    variant = args.configuration.capitalize()
    gradle_command.append('assemble' + variant)
    if args.bundle:
        gradle_command.append('bundle' + variant)
    run(gradle_command)
    output = selected_path(args.output) if args.output else scoped(ROOT / 'dist/android' / (app_id + '-' + build_id + '-' + abi + '-' + args.configuration + '.apk'))
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        raise ValueError('Refusing to overwrite an existing APK: ' + str(output))
    apk_name = 'app-' + args.configuration + ('' if signed else '-unsigned') + '.apk'
    apk = gradle_project / 'app/build/outputs/apk' / args.configuration / apk_name
    with apk.open('rb') as source, output.open('xb') as destination:
        shutil.copyfileobj(source, destination)
    run([SDK / 'build-tools' / LOCK['build_tools'] / 'zipalign.exe', '-c', '-P', '16', '-v', '4', output])
    if signed:
        run([SDK / 'build-tools' / LOCK['build_tools'] / 'apksigner.bat', 'verify', '--verbose', output])
    packaged_native = verify_packaged_library(output, abi, folder / 'packaged-native/libmain.so')
    package_metadata = verify_package_metadata(output, application, bool(icon_report))
    report = {'ok': True, 'apk': str(output), 'sha256': digest(output), 'application_id': app_id,
              'activity': 'org.langmake.player.LangmakeActivity', 'abi': abi,
              'development': args.configuration == 'debug', 'signed': signed,
              'configuration': args.configuration, 'orientation': application['orientation'],
              'application': application, 'icon': icon_report, 'profile': manifest.get('profile'),
              'packaged_application': package_metadata, 'warnings': application['warnings'],
              'source_revision': manifest['source_revision'], 'storage_id': manifest['storage_id'],
              'packaged_revision': manifest['packaged_revision'],
              'gradle_project': str(gradle_project), 'prebuilt_player': portable, 'device_qualified': False}
    report['native_cache_key'] = native_key
    report['native_validation'] = native_validation
    report['packaged_native_validation'] = packaged_native
    if not signed:
        report['note'] = 'Unsigned release build: sign with your own key before installation or distribution.'
    if args.bundle:
        bundle = gradle_project / 'app/build/outputs/bundle' / args.configuration / ('app-' + args.configuration + '.aab')
        bundle_output = output.with_suffix('.aab')
        if bundle_output.exists():
            raise ValueError('Refusing to overwrite existing app bundle.')
        with bundle.open('rb') as source, bundle_output.open('xb') as destination:
            shutil.copyfileobj(source, destination)
        report['bundle'] = str(bundle_output)
        report['bundle_sha256'] = digest(bundle_output)
    output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def verify_package_metadata(apk, application, has_icon):
    response = run([SDK / 'build-tools' / LOCK['build_tools'] / 'aapt.exe', 'dump', 'badging', apk], capture=True)
    text = response.stdout.decode('utf-8', 'replace')
    package = next((line for line in text.splitlines() if line.startswith('package: ')), '')
    values = dict(re.findall(r"([A-Za-z]+)='([^']*)'", package))
    if values.get('name') != application['id'] or values.get('versionCode') != str(application['build']) or values.get('versionName') != application['version']:
        raise ValueError('APK application identity/version differs from resolved game metadata.')
    icons = re.findall(r"^application-icon-[0-9]+:'([^']+)'", text, re.MULTILINE)
    if has_icon and not icons:
        raise ValueError('APK has no packaged launcher icon.')
    with zipfile.ZipFile(apk) as archive:
        resources = []
        names = set(icons) | {name for name in archive.namelist() if name.startswith('res/') and '/ic_launcher' in name}
        for name in sorted(names):
            if name not in archive.namelist(): raise ValueError('APK launcher icon resource is missing: ' + name)
            resources.append({'path': name, 'sha256': hashlib.sha256(archive.read(name)).hexdigest()})
    return {'id': values['name'], 'version': values['versionName'], 'build': int(values['versionCode']), 'icons': resources}


def adb_executable():
    local = SDK / 'platform-tools/adb.exe'
    fallback = shutil.which('adb')
    if local.is_file():
        return local
    if fallback:
        return fallback  # Invoking an existing PATH tool is allowed; no other SDK files are read.
    raise ValueError('adb is unavailable; bootstrap the workspace platform-tools.')


def adb(args, commands, capture=True, input_data=None, require_serial=True, timeout=None):
    command = [adb_executable(), '-P', '5038']
    if require_serial:
        if not args.serial or not SERIAL.fullmatch(args.serial):
            raise ValueError('This operation requires an explicit --serial from devices.')
        command += ['-s', args.serial]
    return run(command + commands, capture=capture, input_data=input_data, **({'timeout': timeout} if timeout is not None else {}))


def devices(args):
    text = adb(args, ['devices', '-l'], require_serial=False).stdout.decode('utf-8', 'replace')
    items = []
    for line in text.splitlines()[1:]:
        fields = line.split()
        if len(fields) >= 2:
            item = {'serial': fields[0], 'state': fields[1], 'details': fields[2:], 'is_emulator': None}
            if fields[1] == 'device' and SERIAL.fullmatch(fields[0]):
                selected = argparse.Namespace(**{**vars(args), 'serial': fields[0]})
                try:
                    marker = adb(selected, ['shell', 'getprop', 'ro.kernel.qemu'], timeout=5).stdout.decode('utf-8', 'replace').strip()
                    if marker in ('0', '1'):
                        item['is_emulator'] = marker == '1'
                    else:
                        item['classification_diagnostic'] = 'ro.kernel.qemu is absent or unrecognized; hardware class is unknown.'
                except (ValueError, RuntimeError, OSError, subprocess.TimeoutExpired) as exc:
                    item['classification_diagnostic'] = str(exc)[:512]
            items.append(item)
    return {'ok': True, 'devices': items, 'note': 'Authorize USB debugging on the selected device when Android asks.'}


def device_command(args):
    app = valid_app(args.application_id) if args.application_id else None
    if args.command == 'install':
        apk = selected_path(args.apk)
        aapt = SDK / 'build-tools' / LOCK['build_tools'] / 'aapt2.exe'
        if not aapt.is_file():
            raise ValueError('Pinned aapt2 is required to verify the APK application ID before installation.')
        badging = run([aapt, 'dump', 'badging', apk], capture=True).stdout.decode('utf-8', 'replace')
        identity = re.search(r"^package: name='([^']+)'", badging, re.MULTILINE)
        if not identity or identity.group(1) != app:
            raise ValueError('APK application ID does not match explicit --application-id; nothing was installed.')
        result = adb(args, ['install', '-r', str(apk)])
    elif args.command == 'launch':
        command = ['shell', 'am', 'start', '-W', '-S', '-n', app + '/org.langmake.player.LangmakeActivity']
        if args.debug_args:
            # A single base64 JSON argument avoids remote-shell interpretation of arbitrary game paths.
            encoded = base64.b64encode(json.dumps(args.debug_args).encode()).decode()
            command += ['--es', 'langmake_debug_args', encoded]
        result = adb(args, command)
    elif args.command == 'stop':
        result = adb(args, ['shell', 'am', 'force-stop', app])
    elif args.command == 'logs':
        pid = adb(args, ['shell', 'pidof', app]).stdout.decode().strip()
        if not re.fullmatch(r'\d+', pid):
            raise ValueError('Selected app is not running; launch it before collecting its logs.')
        result = adb(args, ['logcat', '-d', '--pid=' + pid, '-v', 'threadtime'])
    elif args.command == 'screenshot':
        result = adb(args, ['exec-out', 'screencap', '-p'])
        if not result.stdout.startswith(b'\x89PNG\r\n\x1a\n'):
            raise ValueError('Device screenshot did not return PNG bytes.')
        target = selected_path(args.output)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(result.stdout)
        return {'ok': True, 'serial': args.serial, 'output': str(target), 'sha256': digest(target),
                'kind': 'device screenshot; not an exact simulation-tick capture'}
    elif args.command in ('inspect', 'pull'):
        relative = bridge_path(args.storage_id, args.path if args.command == 'pull' else 'live.json')
        # The shell-v2 protocol preserves the remote exit status and separates
        # stderr from stdout. Some Windows adb shell builds still translate LF
        # to CRLF even with -T. Encode binary pulls on the device, keeping both
        # exact bytes and shell-v2's remote failure status (unlike exec-out).
        result = adb(args, ['shell', '-T', 'run-as', app, 'base64' if args.command == 'pull' else 'cat', relative], timeout=30)
        if args.command == 'pull':
            payload = base64.b64decode(b''.join(result.stdout.split()), validate=True)
            target = selected_path(args.output)
            target.parent.mkdir(parents=True, exist_ok=True)
            temporary = None
            try:
                with tempfile.NamedTemporaryFile(mode='wb', dir=target.parent, prefix='.' + target.name + '.', suffix='.part', delete=False) as stream:
                    temporary = Path(stream.name)
                    stream.write(payload)
                    stream.flush()
                    os.fsync(stream.fileno())
                temporary.replace(target)
            finally:
                if temporary is not None and temporary.exists():temporary.unlink()
            return {'ok': True, 'serial': args.serial, 'output': str(target), 'sha256': digest(target)}
        return {'ok': True, 'serial': args.serial, 'live': json.loads(result.stdout)}
    elif args.command in ('control', 'push'):
        if args.command == 'control':
            command = json.loads(selected_path(args.file).read_text())
            request_id = command.get('id', '') if isinstance(command, dict) else ''
            if not isinstance(request_id, str) or not re.fullmatch(r'[0-9]{1,96}', request_id):
                raise ValueError('Control file requires an id string of 1..96 ASCII digits; the native mailbox uses numeric filenames.')
            relative = bridge_path(args.storage_id, 'commands/' + request_id + '.json')
            payload = (json.dumps(command) + '\n').encode()
        else:
            relative = bridge_path(args.storage_id, args.path)
            payload = selected_path(args.file).read_bytes()
        if len(payload) > 16 * 1024 * 1024:
            raise ValueError('Device transfer exceeds the 16 MiB limit.')
        # Safe fixed paths and shell fragment; arbitrary JSON travels over stdin, never shell interpolation.
        temporary = relative + '.tmp'
        directory = relative.rsplit('/', 1)[0]
        shell = 'mkdir -p ' + directory + ' && cat > ' + temporary + ' && mv ' + temporary + ' ' + relative
        result = adb(args, ['shell', '-T', 'run-as', app, 'sh', '-c', "'" + shell + "'"],
                     input_data=payload)
    else:
        raise ValueError('Unsupported device command.')
    if args.command == 'logs' and args.output:
        target = selected_path(args.output)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(result.stdout)
        return {'ok': True, 'serial': args.serial, 'output': str(target)}
    return {'ok': True, 'serial': args.serial, 'application_id': app, 'output': result.stdout.decode('utf-8', 'replace')}


def bridge_path(storage_id, relative):
    if not re.fullmatch(r'[a-f0-9]{24}', storage_id or ''):
        raise ValueError('Use the 24-character storage_id from the APK build report.')
    name = PurePosixPath(relative)
    if name.is_absolute() or '..' in name.parts or not re.fullmatch(r'[A-Za-z0-9_./-]+', relative):
        raise ValueError('Unsafe app-private relative path.')
    return 'files/state/' + storage_id + '/' + relative


def install_emulator():
    if not (TOOLS / 'sdk-license-acceptance.json').is_file():
        raise ValueError('Explicit SDK license acceptance is required before emulator installation.')
    for package in LOCK['emulator_packages']:
        install_archive(package, SDK / package['destination'], package['archive_root'])
        write_sdk_metadata(package)


def emulator(args):
    binary = SDK / 'emulator/emulator.exe'
    if not binary.is_file():
        raise ValueError('Install the optional emulator with bootstrap --accept-sdk-licenses --emulator first.')
    if args.command == 'emulator-check':
        result = run([binary, '-accel-check'], capture=True, check=False)
        return {'ok': result.returncode == 0, 'output': result.stdout.decode('utf-8', 'replace'),
                'error': result.stderr.decode('utf-8', 'replace'),
                'note': 'This check never installs a hypervisor or changes Windows features.'}
    name = 'langmake-api36'
    avd = CACHE / 'avd' / (name + '.avd')
    if not (avd / 'config.ini').is_file():
        run([SDK / 'cmdline-tools/19.0/bin/avdmanager.bat', 'create', 'avd', '--name', name,
             '--package', 'system-images;android-36;default;x86_64', '--path', avd,
             '--device', 'pixel_2'], input_data=b'no\n')
    log = CACHE / 'emulator.log'
    command = [binary, '-avd', name, '-port', '5556', '-no-snapshot', '-no-audio',
               '-no-boot-anim', '-gpu', 'software', '-accel', args.accel, '-no-metrics']
    if not args.visible:
        command.append('-no-window')
    with log.open('ab') as output:
        child = subprocess.Popen([str(x) for x in command], cwd=ROOT, env=env(), stdout=output,
                                 stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
    (CACHE / 'emulator-process.json').write_text(json.dumps({'pid': child.pid, 'serial': 'emulator-5556', 'log': str(log)}, indent=2))
    return {'ok': True, 'pid': child.pid, 'serial': 'emulator-5556', 'log': str(log),
            'status': 'starting; use devices and wait for Android boot before installing'}


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    boot = sub.add_parser('bootstrap')
    boot.add_argument('--accept-sdk-licenses', action='store_true', help='Explicitly accept the checked-in Google Android SDK license terms.')
    boot.add_argument('--emulator', action='store_true')
    boot.add_argument('--player-only', action='store_true', help='Skip the NDK when exporting with a prebuilt player.')
    doc = sub.add_parser('doctor')
    doc.add_argument('--player-only', action='store_true')
    b = sub.add_parser('build')
    b.add_argument('--project', required=True)
    b.add_argument('--host-build')
    b.add_argument('--player-dir', help='Prebuilt bundle containing <abi>/{debug,release}/libmain.so and shared runtime/{shaders,fonts}; legacy <abi>/libmain.so also supported. Autodetects android-player/.')
    b.add_argument('--shaderc')
    b.add_argument('--native-build')
    b.add_argument('--skip-native', action='store_true')
    b.add_argument('--abi', choices=['arm64-v8a', 'x86_64'], default='arm64-v8a')
    b.add_argument('--jobs', type=int, default=8)
    b.add_argument('--application-id', help='Override application.id; reuse only for updates to the same game.')
    b.add_argument('--label')
    for name in ['version', 'icon', 'profile']: b.add_argument('--' + name)
    b.add_argument('--build-number', type=int)
    b.add_argument('--orientation', choices=['landscape', 'portrait', 'sensor'],
                   help='Allowed screen orientation; sensor supports rotation between portrait and landscape.')
    b.add_argument('--configuration', choices=['debug', 'release'], default='debug')
    b.add_argument('--bundle', action='store_true', help='Also produce an Android App Bundle; this does not publish it.')
    b.add_argument('--keystore', help='Workspace path to a release signing keystore.')
    b.add_argument('--key-alias')
    b.add_argument('--store-password-env', help='Name of an existing environment variable; never the password itself.')
    b.add_argument('--key-password-env', help='Defaults to --store-password-env.')
    b.add_argument('--output')
    sub.add_parser('devices')
    sub.add_parser('emulator-check')
    em = sub.add_parser('emulator-start')
    em.add_argument('--visible', action='store_true', help='Show the interactive emulator window.')
    em.add_argument('--accel', choices=['auto', 'on', 'off'], default='auto')
    for name in ['install', 'launch', 'stop', 'logs', 'screenshot', 'inspect', 'control', 'pull', 'push']:
        d = sub.add_parser(name)
        d.add_argument('--serial', required=True)
        d.add_argument('--application-id', required=name != 'screenshot')
        if name == 'install': d.add_argument('--apk', required=True)
        if name == 'launch': d.add_argument('--debug-args', nargs=argparse.REMAINDER)
        if name in ['logs', 'screenshot', 'pull']: d.add_argument('--output', required=name != 'logs')
        if name in ['inspect', 'control', 'pull', 'push']: d.add_argument('--storage-id', required=True)
        if name in ['control', 'push']: d.add_argument('--file', required=True)
        if name in ['pull', 'push']: d.add_argument('--path', required=True)
    return p


def main():
    args = parser().parse_args()
    try:
        if args.command == 'bootstrap': result = bootstrap(args)
        elif args.command == 'doctor': result = doctor(args)
        elif args.command == 'build': result = build(args)
        elif args.command == 'devices': result = devices(args)
        elif args.command.startswith('emulator-'): result = emulator(args)
        else: result = device_command(args)
        print(json.dumps(result, indent=2))
        return 0 if result.get('ok') else 2
    except (ValueError, RuntimeError, OSError, subprocess.SubprocessError, zipfile.BadZipFile) as error:
        print(json.dumps({'ok': False, 'error': str(error)}, indent=2))
        return 1


if __name__ == '__main__':
    sys.exit(main())
