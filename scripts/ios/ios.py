"""Standalone, workspace-local iOS staging and Xcode deployment tools.

Staging works on Windows. Compilation, signing and deployment require an explicitly
selected Mac workspace with full Xcode; no Apple SDK or signing account is installed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
import time
import uuid
import shlex

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import mobile_assets

ROOT = Path(__file__).resolve().parents[2]
CACHE = ROOT / '.cache/ios'
LIMIT = 256 * 1024 * 1024
FILE_LIMIT = 16384
IDENTIFIER = re.compile(r'[A-Za-z][A-Za-z0-9-]*(?:\.[A-Za-z0-9][A-Za-z0-9-]*)+')
DEVICE = re.compile(r'[A-Fa-f0-9-]{8,64}')
STORAGE = re.compile(r'[a-f0-9]{24}')
TOOL_HELP = {}


class AppFileNotFoundError(RuntimeError):
    """Absence confirmed within the selected app container, not a transport guess."""
    def __init__(self, path, evidence):
        super().__init__('App file not found: ' + path)
        self.path, self.evidence = path, evidence


def scoped(path):
    value = Path(path)
    value = (value if value.is_absolute() else ROOT / value).resolve()
    try:
        value.relative_to(ROOT.resolve())
    except ValueError:
        raise ValueError('Path must stay inside this Langmake workspace: ' + str(value))
    return value


def relative_path(value):
    if not isinstance(value, str) or not value or len(value) > 1024:
        raise ValueError('A nonempty, bounded relative path is required.')
    parts = PurePosixPath(value)
    if (parts.is_absolute() or '\\' in value or ':' in value or '\0' in value or
            any(part in ('', '.', '..') for part in value.split('/')) or
            any(ord(char) < 32 for char in value)):
        raise ValueError('Unsafe relative path: ' + repr(value))
    return value


def contained_file(root, relative):
    relative_path(relative)
    root = Path(root).resolve()
    candidate = root / relative
    for component in [candidate] + list(candidate.parents):
        if component == root:
            break
        if component.is_symlink():
            raise ValueError('Symlinks are not allowed in staged game content: ' + relative)
    candidate = candidate.resolve()
    candidate.relative_to(root)
    if not candidate.is_file():
        raise ValueError('Required content file is missing: ' + relative)
    return candidate


def bundle_id(value):
    if not IDENTIFIER.fullmatch(value or '') or len(value) > 255:
        raise ValueError('Use a reverse-DNS bundle ID, for example org.example.mygame.')
    return value


def digest(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()


def source_revision(path):
    value = 14695981039346656037
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            for byte in chunk:
                value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return 'fnv1a64-%016x' % value


def read_json(path, limit=8 * 1024 * 1024):
    if Path(path).stat().st_size > limit:
        raise ValueError('JSON exceeds the supported size: ' + str(path))
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.new-' + uuid.uuid4().hex)
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    temporary.replace(path)


def env():
    result = os.environ.copy()
    # Xcode's installed SDKs, accounts and keychain are external prerequisites.
    # Do not change HOME: that would silently select different signing identities.
    for name, path in {'TMPDIR': CACHE / 'tmp', 'TMP': CACHE / 'tmp', 'TEMP': CACHE / 'tmp',
                       'CLANG_MODULE_CACHE_PATH': CACHE / 'module-cache',
                       'SWIFT_MODULECACHE_PATH': CACHE / 'swift-cache'}.items():
        path.mkdir(parents=True, exist_ok=True)
        result[name] = str(path)
    return result


def run(command, capture=True, check=True, timeout=None):
    command = [str(item) for item in command]
    print('+ ' + subprocess.list2cmdline(command), file=sys.stderr)
    result = subprocess.run(command, cwd=ROOT, env=env(), stdout=subprocess.PIPE if capture else sys.stderr,
                            stderr=subprocess.PIPE if capture else sys.stderr, timeout=timeout)
    if check and result.returncode:
        diagnostic = ((result.stderr or b'') + (result.stdout or b'')).decode('utf-8', 'replace')[-12000:]
        raise RuntimeError('Command failed (%s): %s\n%s' % (result.returncode, command[0], diagnostic))
    return result


def require_mac():
    if sys.platform != 'darwin':
        raise ValueError('iOS compilation, signing and deployment require a Mac with full Xcode. '
                         'Run stage here, copy the staged folder and Langmake source workspace to an authorized Mac workspace, then run build there.')


def doctor(args=None):
    checks = [{'name': 'macOS', 'ok': sys.platform == 'darwin', 'actual': platform.platform()}]
    if sys.platform != 'darwin':
        return {'ok': False, 'can_stage': True, 'can_build': False, 'checks': checks,
                'reason': 'Windows/Linux can stage game content; iOS builds and device operations require macOS with full Xcode.',
                'minimum_ios': '17.0', 'qualification': 'Mac and iPhone verification is required; staging is not an iOS build.'}
    for executable in ['xcodebuild', 'xcrun', 'cmake', 'git']:
        checks.append({'name': executable, 'ok': bool(shutil.which(executable)), 'path': shutil.which(executable)})
    if shutil.which('xcodebuild') and shutil.which('xcrun'):
        for name, command in [('Xcode', ['xcodebuild', '-version']),
                              ('Xcode first-launch setup', ['xcodebuild', '-checkFirstLaunchStatus']),
                              ('iPhoneOS SDK', ['xcrun', '--sdk', 'iphoneos', '--show-sdk-version']),
                              ('iPhoneSimulator SDK', ['xcrun', '--sdk', 'iphonesimulator', '--show-sdk-version']),
                              ('devicectl', ['xcrun', '--find', 'devicectl']),
                              ('simctl', ['xcrun', '--find', 'simctl'])]:
            response = run(command, check=False, timeout=30)
            output = response.stdout.decode('utf-8', 'replace').strip()
            ok = response.returncode == 0
            if name.endswith('SDK') and ok:
                ok = bool(re.match(r'^(?:1[7-9]|[2-9][0-9])(?:\.|$)', output))
            error = response.stderr.decode('utf-8', 'replace').strip()
            if name == 'Xcode first-launch setup' and not ok:
                guidance = ('Xcode first-launch setup is incomplete or could not be verified. '
                            'Open the selected Xcode app on the Mac and complete its setup, then rerun doctor. '
                            'Langmake does not accept licenses or install Xcode components.')
                error = error + '\n' + guidance if error else guidance
            checks.append({'name': name, 'ok': ok, 'actual': output, 'error': error})
    ok = all(item['ok'] for item in checks)
    return {'ok': ok, 'can_stage': True, 'can_build': ok, 'checks': checks, 'minimum_ios': '17.0',
            'signing': 'Device builds require your configured Apple signing team and provisioning. This tool never logs in or accepts Xcode terms.',
            'simulator_device_set': str(CACHE / 'simulators')}


def host_executable(folder=None):
    candidates = [scoped(folder) / ('langmake.exe' if os.name == 'nt' else 'langmake')] if folder else [
        ROOT / 'build' / ('langmake.exe' if os.name == 'nt' else 'langmake'),
        ROOT / ('langmake.exe' if os.name == 'nt' else 'langmake')]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise ValueError('A host Langmake CLI supporting cook --target ios is required to stage a project. Specify --host-build DIR.')


def runtime_sources():
    packaged = ROOT / 'ios-player/runtime'
    if packaged.is_dir():
        return [(str(path.relative_to(packaged).as_posix()), path) for path in sorted(packaged.rglob('*')) if path.is_file()]
    source = ROOT / '.deps/source'
    files = [('shaders/' + name + '.bin', source / ('bgfx.cmake/bgfx/examples/runtime/shaders/metal/' + name + '.bin'))
             for name in ['vs_cubes', 'fs_cubes']]
    files.append(('fonts/LatoLatin-Regular.ttf', source / 'RmlUi/Samples/assets/LatoLatin-Regular.ttf'))
    for name in ['LICENSE', 'THIRD_PARTY_NOTICES.md', 'dependencies.lock.json']:
        files.append(('licenses/' + name, ROOT / name))
    licenses = ROOT / 'licenses'
    if not licenses.is_dir():
        raise ValueError('The engine license inventory is missing.')
    files += [('licenses/third-party/' + path.relative_to(licenses).as_posix(), path)
              for path in sorted(licenses.rglob('*')) if path.is_file()]
    return files


def stage_cooked(cooked, output, identifier=None, label=None, orientation=None, configuration='debug', resources=None,
                 version=None, build_number=None, icon=None):
    cooked, output = scoped(cooked), scoped(output)
    project = read_json(cooked / 'project.json')
    application = mobile_assets.metadata(project, 'ios', configuration, {
        'id': identifier, 'label': label, 'orientation': orientation, 'version': version, 'build': build_number, 'icon': icon})
    identifier, label, orientation = application['id'], application['label'], application['orientation']
    if configuration not in ['debug', 'release']:
        raise ValueError('Unsupported orientation or configuration.')
    if not label or len(label) > 128 or any(ord(c) < 32 for c in label):
        raise ValueError('The app label must contain 1–128 printable characters.')
    if output.exists():
        raise ValueError('Refusing to overwrite an existing staged project: ' + str(output))
    cook = read_json(cooked / 'cook-manifest.json')
    if cook.get('format_version') != 1 or cook.get('target') != 'ios' or cook.get('project_manifest') != 'project.json':
        raise ValueError('An iOS cook version-1 bundle with project.json is required.')
    names = ['cook-manifest.json']
    sizes, revisions = {}, {}
    for entry in cook.get('files', []):
        name = relative_path(entry['path'])
        if name in names or name.casefold() in {item.casefold() for item in names}:
            raise ValueError('Duplicate or case-colliding cooked file: ' + name)
        if not isinstance(entry.get('bytes'), int) or isinstance(entry.get('bytes'), bool) or entry['bytes'] < 0:
            raise ValueError('Invalid cook byte count: ' + name)
        names.append(name)
        sizes[name] = entry['bytes']
        revisions[name] = entry.get('revision')
    if 'project.json' not in names or len(names) > FILE_LIMIT:
        raise ValueError('Cook source graph is missing project.json or exceeds the file limit.')
    planned = [('project/' + name, contained_file(cooked, name)) for name in names]
    for name, file in planned:
        if name[8:] in sizes and (file.stat().st_size != sizes[name[8:]] or source_revision(file) != revisions[name[8:]]):
            raise ValueError('Cooked content changed after validation: ' + name)
    resources = resources if resources is not None else runtime_sources()
    for relative, path in resources:
        relative_path(relative)
        if Path(path).is_symlink():
            raise ValueError('Runtime resources must not be symlinks: ' + str(path))
        path = scoped(path)
        if not path.is_file() or path.is_symlink():
            raise ValueError('Missing regular runtime resource: ' + str(path))
        planned.append(('runtime/' + relative, path))
    expected = ['runtime/shaders/vs_cubes.bin', 'runtime/shaders/fs_cubes.bin', 'runtime/fonts/LatoLatin-Regular.ttf']
    if any(name not in [entry[0] for entry in planned] for name in expected):
        raise ValueError('The iOS runtime must contain the Metal cube shaders and bundled font.')
    if len(planned) > FILE_LIMIT or len({name.casefold() for name, _ in planned}) != len(planned):
        raise ValueError('Too many files or duplicate runtime paths.')
    total = sum(path.stat().st_size for _, path in planned)
    if total > LIMIT:
        raise ValueError('iOS content exceeds the 256 MiB budget.')
    icon_source = mobile_assets.icon_path(cooked, application['icon']) if application.get('icon') else None
    if icon_source:
        if application['icon'] not in names:
            raise ValueError('Icon must belong to the cooked source graph; declare application.icon and cook again.')
        mobile_assets.decode_png(icon_source, 1024)
    identity = project.get('project_id') or project.get('title', 'game')
    if not isinstance(identity, str) or not identity:
        raise ValueError('Project identity must be a nonempty string.')
    manifest = {'format_version': 1, 'target': 'ios', 'minimum_ios': '17.0', 'bundle_identifier': identifier,
                'label': label, 'orientation': orientation, 'development': configuration == 'debug',
                'configuration': configuration, 'project_id': identity, 'project_manifest': 'project/project.json',
                'storage_id': hashlib.sha256(identity.encode('utf-8')).hexdigest()[:24],
                'source_revision': cook['source_revision'], 'packaged_revision': cook.get('packaged_revision', cook['source_revision']),
                'content_bytes': total, 'files': []}
    manifest['application'] = application
    manifest['profile'] = project.get('default_profile')
    temporary = scoped(output.with_name(output.name + '.stage-' + uuid.uuid4().hex))
    temporary.mkdir(parents=True)
    try:
        for name, source in sorted(planned):
            target = temporary / name
            target.parent.mkdir(parents=True, exist_ok=True)
            with source.open('rb') as stream, target.open('xb') as destination:
                shutil.copyfileobj(stream, destination, 1024 * 1024)
            if name.startswith('project/') and name[8:] in revisions and source_revision(target) != revisions[name[8:]]:
                raise ValueError('Cooked content changed while staging: ' + name)
            manifest['files'].append({'path': name, 'bytes': target.stat().st_size, 'sha256': digest(target)})
        if icon_source:
            icon_report = mobile_assets.generate_icon(icon_source, temporary / 'runtime/application', 'ios')
            manifest['icon'] = icon_report
            for item in icon_report['files']:
                manifest['files'].append(dict(item, path='runtime/application/' + item['path']))
                manifest['content_bytes'] += item['bytes']
        manifest['content_id'] = hashlib.sha256(json.dumps(manifest, sort_keys=True, separators=(',', ':')).encode('utf-8')).hexdigest()
        write_json(temporary / 'manifest.json', manifest)
        verify_stage(temporary)
        temporary.rename(output)
    except Exception:
        # `temporary` was resolved and checked inside ROOT before creation.
        shutil.rmtree(temporary, ignore_errors=True)
        raise
    return {'ok': True, 'staged': str(output), 'bundle_id': identifier, 'configuration': configuration,
            'orientation': orientation, 'label': label, 'storage_id': manifest['storage_id'], 'content_id': manifest['content_id'],
            'source_revision': manifest['source_revision'], 'packaged_revision': manifest['packaged_revision'],
            'content_bytes': manifest['content_bytes'], 'files': len(manifest['files']), 'device_qualified': False,
            'application': application, 'icon': manifest.get('icon'), 'profile': manifest['profile'], 'warnings': application['warnings']}


def verify_stage(folder):
    folder = scoped(folder)
    manifest = read_json(folder / 'manifest.json')
    if manifest.get('format_version') != 1 or manifest.get('target') != 'ios':
        raise ValueError('Unsupported iOS content manifest.')
    bundle_id(manifest.get('bundle_identifier'))
    if not STORAGE.fullmatch(manifest.get('storage_id', '')):
        raise ValueError('Invalid stable project storage ID.')
    if manifest.get('configuration') not in ('debug', 'release') or manifest.get('development') is not (manifest.get('configuration') == 'debug'):
        raise ValueError('Development mode and configuration disagree.')
    if manifest.get('orientation') not in ('landscape', 'portrait', 'sensor'):
        raise ValueError('Invalid orientation in content manifest.')
    entries = manifest.get('files')
    if not isinstance(entries, list) or not entries or len(entries) > FILE_LIMIT:
        raise ValueError('Invalid content file inventory.')
    seen, exact_names = set(), set()
    total = 0
    for entry in entries:
        name = relative_path(entry['path'])
        if not name.startswith(('project/', 'runtime/')) or name.casefold() in seen:
            raise ValueError('Duplicate or unsupported content path: ' + name)
        seen.add(name.casefold())
        exact_names.add(name)
        source = contained_file(folder, name)
        size = source.stat().st_size
        total += size
        if size != entry.get('bytes') or total > LIMIT or not re.fullmatch('[a-f0-9]{64}', entry.get('sha256', '')) or digest(source) != entry['sha256']:
            raise ValueError('Content integrity or byte budget failed: ' + name)
    for name in ['project/project.json', 'runtime/shaders/vs_cubes.bin', 'runtime/shaders/fs_cubes.bin', 'runtime/fonts/LatoLatin-Regular.ttf']:
        if name not in exact_names:
            raise ValueError('Missing mandatory bundle resource: ' + name)
    if total != manifest.get('content_bytes'):
        raise ValueError('Content byte total disagrees with inventory.')
    unsigned = {key: value for key, value in manifest.items() if key != 'content_id'}
    if hashlib.sha256(json.dumps(unsigned, sort_keys=True, separators=(',', ':')).encode('utf-8')).hexdigest() != manifest.get('content_id'):
        raise ValueError('Content manifest checksum disagrees.')
    actual = set()
    for path in folder.rglob('*'):
        if path.is_symlink():
            raise ValueError('Content directory must not include symlinks.')
        if path.is_file():
            actual.add(path.relative_to(folder).as_posix().casefold())
    if actual != seen | {'manifest.json'}:
        raise ValueError('Unregistered files exist in the staged content directory.')
    return manifest


def stage(args):
    output = scoped(args.output)
    if output.exists():
        raise ValueError('Staging requires a new output directory.')
    project = scoped(args.project)
    host = host_executable(args.host_build)
    work = scoped(CACHE / 'cook' / uuid.uuid4().hex)
    work.parent.mkdir(parents=True, exist_ok=True)
    command = [host, 'cook', '--project', project, '--output', work, '--target', 'ios']
    if getattr(args, 'profile', None): command += ['--profile', args.profile]
    run(command)
    return stage_cooked(work, output, args.bundle_id, args.label, args.orientation, args.configuration,
                        version=getattr(args, 'version', None), build_number=getattr(args, 'build_number', None),
                        icon=getattr(args, 'icon', None))


def build_command(args, manifest, work, shaderc):
    device = getattr(args, 'device', None)
    register_device = getattr(args, 'register_device', False)
    if args.destination == 'device' and args.arch != 'arm64':
        raise ValueError('Physical iOS devices require --arch arm64.')
    if args.destination == 'device' and not re.fullmatch(r'[A-Z0-9]{10}', args.team_id or ''):
        raise ValueError('Device builds require your explicit 10-character --team-id and an existing Xcode signing account.')
    if args.destination == 'simulator' and any([args.team_id, args.signing_identity, args.provisioning_profile, args.allow_provisioning_updates, device, register_device]):
        raise ValueError('Signing/team, --device and --register-device options apply only to a physical-device build.')
    if register_device and (not device or not args.allow_provisioning_updates):
        raise ValueError('--register-device requires an explicit --device and --allow-provisioning-updates.')
    if device:
        selected_device(args)
    sdk = 'iphoneos' if args.destination == 'device' else 'iphonesimulator'
    command = ['cmake', '-S', str(ROOT), '-B', str(work), '-G', 'Xcode', '-DCMAKE_SYSTEM_NAME=iOS',
               '-DCMAKE_OSX_SYSROOT=' + sdk, '-DCMAKE_OSX_ARCHITECTURES=' + args.arch,
               '-DCMAKE_OSX_DEPLOYMENT_TARGET=17.0', '-DBUILD_TESTING=OFF',
               '-DLM_HOST_SHADERC=' + str(shaderc), '-DLM_IOS_CONTENT_DIRECTORY=' + str(scoped(args.staged)),
               '-DLM_IOS_DEVELOPMENT=' + ('ON' if manifest['development'] else 'OFF'),
               '-DLM_IOS_BUNDLE_IDENTIFIER=' + manifest['bundle_identifier'],
               '-DLM_IOS_LABEL=' + manifest['label'], '-DLM_IOS_ORIENTATION=' + manifest['orientation']]
    application = manifest.get('application', {})
    command += ['-DLM_IOS_VERSION=' + application.get('version', mobile_assets.engine_version()),
                '-DLM_IOS_BUILD_NUMBER=' + str(application.get('build', 401))]
    if args.destination == 'device':
        command += ['-DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=' + args.team_id,
                    '-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY=' + (args.signing_identity or 'Apple Development'),
                    '-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_STYLE=' + ('Manual' if args.provisioning_profile else 'Automatic')]
        if args.provisioning_profile:
            command += ['-DCMAKE_XCODE_ATTRIBUTE_PROVISIONING_PROFILE_SPECIFIER=' + args.provisioning_profile]
        if device:
            command += ['-DCMAKE_XCODE_GENERATE_SCHEME=ON']
    else:
        command += ['-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO']
    return command


def native_build_command(args, manifest, work, device=None):
    configuration = manifest['configuration'].capitalize()
    if device is not None:
        # xcodebuild ignores -destination with -target. A scheme is required to
        # provision for the selected phone, whose hardware UDID differs from its
        # CoreDevice identifier used by devicectl.
        command = ['xcodebuild', '-project', str(work / 'Langmake.xcodeproj'), '-scheme', 'langmake',
                   '-configuration', configuration, '-destination', 'id=' + device['udid'],
                   '-derivedDataPath', str(work / 'DerivedData'), '-jobs', str(args.jobs)]
        if args.allow_provisioning_updates:
            command += ['-allowProvisioningUpdates']
        if getattr(args, 'register_device', False):
            command += ['-allowProvisioningDeviceRegistration']
        return command + ['build']
    command = ['cmake', '--build', work, '--config', configuration, '--target', 'langmake', '--parallel', str(args.jobs)]
    if args.allow_provisioning_updates:
        command += ['--', '-allowProvisioningUpdates']
    return command


def host_tools_command(host):
    version = platform.mac_ver()[0]
    if not re.fullmatch(r'(?:10|[1-9][0-9])\.[0-9]{1,2}(?:\.[0-9]{1,2})?', version):
        raise ValueError('Cannot determine the running macOS version for the host shader compiler: ' + repr(version))
    # Xcode may contain an SDK newer than this Mac. Its default deployment
    # target can otherwise produce a shaderc that cannot run on the build host.
    return ['cmake', '-S', ROOT, '-B', host, '-G', 'Xcode', '-DLM_HOST_TOOLS_ONLY=ON',
            '-DBUILD_TESTING=OFF', '-DCMAKE_OSX_DEPLOYMENT_TARGET=' + version]


def native_cache_key(args, manifest, toolchain, shaderc):
    paths = [ROOT / 'CMakeLists.txt', ROOT / 'dependencies.lock.json']
    for directory in ['src', 'include', 'cmake', 'shaders', 'platform/ios']:
        paths += sorted(path for path in (ROOT / directory).rglob('*') if path.is_file())
    sources = [(path.relative_to(ROOT).as_posix(), digest(path)) for path in paths if path.is_file()]
    inputs = {'sources': sources, 'destination': args.destination, 'arch': args.arch,
              'configuration': manifest['configuration'], 'shaderc': digest(shaderc),
              'team': args.team_id, 'identity': args.signing_identity, 'profile': args.provisioning_profile,
              'toolchain': [(item['name'], item.get('actual')) for item in toolchain.get('checks', [])
                            if item['name'] in ['Xcode', 'iPhoneOS SDK', 'iPhoneSimulator SDK']]}
    return hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()


def operation_path(identifier):
    if not isinstance(identifier, str) or not re.fullmatch('[a-f0-9]{32}', identifier):
        raise ValueError('Select the 32-character operation ID returned by build.')
    return scoped(CACHE / 'operations' / identifier / 'operation.json')


def save_operation(operation):
    path = operation_path(operation['operation_id'])
    operation['operation_path'] = str(path)
    operation['updated_at_ms'] = time.time_ns() // 1000000
    write_json(path, operation)
    return operation


def build_attempt(operation, phase, status, **values):
    attempts = operation.setdefault('attempts', [])
    number = len(attempts) + 1
    record = dict(phase=phase, status=status, attempt=number, time_ms=time.time_ns() // 1000000, **values)
    path = operation_path(operation['operation_id']).parent / ('attempt-%04d.json' % number)
    if path.exists():
        raise ValueError('Refusing to replace original operation attempt evidence.')
    write_json(path, record)
    attempts.append(dict(record, path=str(path)))
    return save_operation(operation)


def blocked_operation(operation, code, message, next_step, resumable=False):
    operation.update(ok=False, status='blocked', resumable=resumable,
                     blocking={'code': code, 'message': message, 'next_step': next_step})
    save_operation(operation)
    return operation_summary(operation)


def operation_summary(operation):
    keys = ['ok', 'status', 'operation_id', 'operation_path', 'resumable', 'blocking', 'helper', 'report',
            'app', 'bundle_id', 'source_revision', 'content_id', 'build_directory', 'native_cache_key']
    summary = {key: operation[key] for key in keys if key in operation}
    if isinstance(summary.get('blocking'), dict):
        blocking = dict(summary['blocking'])
        message = str(blocking.get('message', ''))
        if len(message) > 512:
            explanations = {
                'signing_interaction_required': 'Compilation produced a verified app; signing needs local interaction.',
                'native_build_failed': 'The native build failed.',
                'apple_agreement_required': 'Apple agreement or license acceptance is required.',
                'signing_identity_unavailable': 'The selected signing identity is unavailable.',
                'provisioning_required': 'The selected app needs a provisioning profile.',
                'missing_sdk': 'The required Apple SDK is unavailable.',
            }
            explanation = explanations.get(blocking.get('code'), 'The operation is blocked.')
            lines = [' '.join(line.split()) for line in message.splitlines() if line.strip()]
            relevant = [line for line in lines if any(token in line.lower() for token in ('error:', 'errsec', 'failed', 'no signing certificate'))]
            excerpt = (relevant or lines or [''])[0]
            detail = explanation + (' ' + excerpt if excerpt else '')
            suffix = ' Full diagnostics remain in the operation record and attempt logs.'
            blocking['message'] = detail[:512-len(suffix)-3].rstrip() + '...' + suffix
            blocking['message_truncated'] = True
        summary['blocking'] = blocking
    return summary


def inspect_operation(args):
    operation = read_json(operation_path(args.operation))
    if getattr(args, 'full', False): return operation
    return dict(operation_summary(operation), attempts=len(operation.get('attempts', [])))


def app_files(app):
    result = {}
    for path in sorted(Path(app).rglob('*')):
        if path.is_symlink():
            raise ValueError('Generated iOS app must not contain symlinks.')
        if path.is_file() and '_CodeSignature' not in path.relative_to(app).parts:
            result[path.relative_to(app).as_posix()] = digest(path)
    return result


def verify_app(app, manifest, arch, signed):
    actual_manifest = verify_stage(app / 'langmake')
    if actual_manifest != manifest:
        raise ValueError('Built app content differs from the staged source.')
    with (app / 'Info.plist').open('rb') as stream:
        info = plistlib.load(stream)
    if info.get('CFBundleIdentifier') != manifest['bundle_identifier']:
        raise ValueError('Built app bundle identifier differs from staged content.')
    application = manifest.get('application', {})
    for key, expected in [('CFBundleShortVersionString', application.get('version')),
                          ('CFBundleVersion', str(application['build']) if 'build' in application else None)]:
        if expected is not None and info.get(key) != expected:
            raise ValueError('Built app metadata differs from staged application: ' + key)
    if manifest.get('icon') and (not (app / 'Assets.car').is_file() or not info.get('CFBundleIcons', {}).get('CFBundlePrimaryIcon', {}).get('CFBundleIconName')):
        raise ValueError('Built app is missing its compiled AppIcon asset catalog or plist icon declaration.')
    executable_name = relative_path(info.get('CFBundleExecutable', ''))
    if '/' in executable_name:
        raise ValueError('The native app executable must be at the bundle root.')
    executable = contained_file(app, executable_name)
    architectures = run(['xcrun', 'lipo', '-archs', executable]).stdout.decode('ascii').split()
    if architectures != [arch]:
        raise ValueError('The built Mach-O architecture differs from the selected target: ' + ', '.join(architectures))
    if signed:
        run(['codesign', '--verify', '--deep', '--strict', app])
    return executable


def signing_recovery(operation, app, diagnostic):
    # Preserve only a signing command Xcode actually selected for this exact app.
    # Never guess an identity, synthesize entitlements or bypass Keychain access.
    identity, entitlements = None, None
    for line in diagnostic.splitlines():
        if '/usr/bin/codesign ' not in line:
            continue
        try:
            command = shlex.split(line.strip())
            if Path(command[-1]).resolve() != app.resolve(): continue
            selected = command[command.index('--sign') + 1]
            entitlement_path = scoped(command[command.index('--entitlements') + 1])
            if re.fullmatch('[A-Fa-f0-9]{40}', selected) and entitlement_path.is_file():
                identity, entitlements = selected, entitlement_path
        except (ValueError, IndexError):
            continue
    if not identity or not (app / 'embedded.mobileprovision').is_file():
        return False
    folder = operation_path(operation['operation_id']).parent
    candidate = folder / app.name
    shutil.copytree(app, candidate)
    shutil.copyfile(entitlements, folder / 'signing-entitlements.plist')
    operation['signing'] = {'identity': identity, 'entitlements': str(folder / 'signing-entitlements.plist'),
                            'entitlements_sha256': digest(folder / 'signing-entitlements.plist')}
    operation['candidate'] = str(candidate)
    operation['candidate_files'] = app_files(candidate)
    helper = folder / 'sign-local.sh'
    command = [sys.executable, str(Path(__file__).resolve()), 'resume', '--operation', operation['operation_id']]
    helper.write_bytes(('#!/bin/sh\nset -eu\nexec ' + ' '.join(shlex.quote(item) for item in command) + '\n').encode('utf-8'))
    helper.chmod(0o700)
    operation['helper'] = str(helper)
    return True


def finish_build(operation, app, manifest):
    signed = operation['destination'] == 'device'
    executable = verify_app(app, manifest, operation['arch'], signed)
    output = scoped(operation['output'])
    if output.exists():
        raise ValueError('Build output must be a new directory.')
    output.mkdir(parents=True)
    exported = output / app.name
    shutil.copytree(app, exported)
    # Verify the exported copy, including content and signature, before reporting it.
    verify_app(exported, manifest, operation['arch'], signed)
    report = {'ok': True, 'status': 'complete', 'app': str(exported), 'bundle_id': manifest['bundle_identifier'],
              'destination': operation['destination'], 'arch': operation['arch'], 'configuration': manifest['configuration'],
              'development': manifest['development'], 'signed': signed, 'orientation': manifest['orientation'],
              'selected_device': operation['selected_device'], 'provisioning_device_registration_requested': operation['register_device'],
              'storage_id': manifest['storage_id'], 'source_revision': manifest['source_revision'],
              'packaged_revision': manifest['packaged_revision'], 'content_id': manifest['content_id'],
              'application': manifest.get('application'), 'icon': manifest.get('icon'), 'profile': manifest.get('profile'),
              'build_directory': operation['build_directory'], 'shaderc_sha256': operation['shaderc_sha256'],
              'toolchain': operation['toolchain'], 'native_cache_key': operation['native_cache_key'],
              'executable_sha256': digest(executable), 'executable_bytes': executable.stat().st_size,
              'dependency_lock_sha256': digest(ROOT / 'dependencies.lock.json'), 'artifact_files': app_files(exported),
              'operation_id': operation['operation_id'], 'operation_path': str(operation_path(operation['operation_id'])),
              'device_qualified': False, 'report': str(output / 'build-report.json')}
    write_json(output / 'build-report.json', report)
    operation.update(report, resumable=False)
    operation.pop('blocking', None)
    build_attempt(operation, 'verification', 'complete', artifact=str(exported))
    return {key: value for key, value in report.items() if key != 'artifact_files'}


def resume(args):
    require_mac()
    operation = read_json(operation_path(args.operation))
    manifest = verify_stage(operation['staged'])
    if manifest['content_id'] != operation['content_id']:
        raise ValueError('Staged content changed since the original build; create a new build operation.')
    if operation.get('status') == 'complete':
        app = scoped(operation['app'])
        verify_app(app, manifest, operation['arch'], operation['destination'] == 'device')
        if app_files(app) != operation['artifact_files']:
            raise ValueError('Completed artifact changed since verification.')
        return {key: value for key, value in read_json(scoped(operation['report'])).items() if key != 'artifact_files'}
    if not operation.get('resumable') or not operation.get('signing'):
        raise ValueError('This operation has no verified signing candidate. Follow its blocking next_step, then start a new build; the native cache is retained.')
    app = scoped(operation['candidate'])
    if app_files(app) != operation['candidate_files']:
        raise ValueError('Signing candidate changed; refusing to sign modified content.')
    verify_app(app, manifest, operation['arch'], False)
    signing = operation['signing']
    entitlements = scoped(signing['entitlements'])
    if digest(entitlements) != signing['entitlements_sha256'] or not re.fullmatch('[A-Fa-f0-9]{40}', signing['identity']):
        raise ValueError('Original signing evidence changed.')
    # codesign can modify a Mach-O before failing. Sign an attempt copy so the
    # verified original remains unchanged and future resumes are still possible.
    attempt_directory = operation_path(args.operation).parent / ('signing-candidate-' + uuid.uuid4().hex)
    attempt_directory.mkdir()
    candidate = attempt_directory / app.name
    shutil.copytree(app, candidate)
    command = ['codesign', '--force', '--sign', signing['identity'], '--entitlements', entitlements,
               '--generate-entitlement-der', candidate]
    result = run(command, check=False)
    diagnostic = ((result.stdout or b'') + (result.stderr or b'')).decode('utf-8', 'replace')
    log = operation_path(args.operation).parent / ('sign-%04d.log' % (len(operation['attempts']) + 1))
    log.write_bytes(diagnostic.encode('utf-8'))
    build_attempt(operation, 'signing', 'failed' if result.returncode else 'complete', returncode=result.returncode, log=str(log))
    if result.returncode:
        return blocked_operation(operation, 'signing_interaction_required', diagnostic[-4000:],
                                 'Run the generated helper locally on the Mac and approve the Keychain prompt. No password is read by Langmake.', True)
    try:
        return finish_build(operation, candidate, manifest)
    except (ValueError, OSError, RuntimeError, subprocess.SubprocessError) as error:
        build_attempt(operation, 'verification', 'failed', error=str(error))
        return blocked_operation(operation, 'artifact_verification_failed', str(error),
                                 'Inspect the verification evidence. The original signing candidate and all attempts are preserved; do not deploy this unverified artifact.')


def build(args):
    manifest = verify_stage(args.staged)
    output = scoped(args.output) if args.output else scoped(ROOT / 'dist/ios' / (manifest['bundle_identifier'] + '-' + uuid.uuid4().hex[:12]))
    if output.exists():
        raise ValueError('Build output must be a new directory.')
    # Validate destination/signing before any build or dependency download.
    build_command(args, manifest, CACHE / 'placeholder', CACHE / 'placeholder/shaderc')
    require_mac()
    selected = None
    if getattr(args, 'device', None):
        selected = require_physical_device(args)
        if not isinstance(selected.get('udid'), str) or not DEVICE.fullmatch(selected['udid']):
            raise ValueError('Selected physical device has no valid hardware UDID for Xcode provisioning. Reconnect/pair the iPhone or iPad and run devices --destination device; do not substitute its CoreDevice id for the hardware UDID.')
    operation = {'ok': False, 'status': 'checking_prerequisites', 'operation_id': uuid.uuid4().hex,
                 'staged': str(scoped(args.staged)), 'output': str(output), 'content_id': manifest['content_id'],
                 'bundle_id': manifest['bundle_identifier'], 'source_revision': manifest['source_revision'],
                 'destination': args.destination, 'arch': args.arch, 'selected_device': selected,
                 'register_device': bool(getattr(args, 'register_device', False))}
    save_operation(operation)
    report = doctor(args)
    if not report['can_build']:
        operation['toolchain'] = report
        build_attempt(operation, 'prerequisites', 'failed')
        first_launch = next((item for item in report.get('checks', []) if item['name'] == 'Xcode first-launch setup' and not item['ok']), None)
        code = 'apple_agreement_or_setup_required' if first_launch else 'missing_sdk_or_toolchain'
        return blocked_operation(operation, code, 'The selected Mac toolchain is incomplete.',
                                 'Run doctor, complete the reported Xcode setup, then start build again. Langmake does not accept Apple agreements.')
    try:
        if not (ROOT / 'CMakeLists.txt').is_file():
            raise ValueError('iOS builds require the Langmake source workspace with pinned dependencies. This is not a prebuilt iOS-player distribution.')
        shaderc = scoped(args.shaderc) if args.shaderc else CACHE / 'host-tools/Release/shaderc'
        if not args.shaderc:
            host = scoped(CACHE / 'host-tools')
            run(host_tools_command(host), capture=False)
            run(['cmake', '--build', host, '--config', 'Release', '--target', 'shaderc', '--parallel', str(args.jobs)], capture=False)
            candidates = [path for path in host.rglob('shaderc') if path.is_file() and os.access(path, os.X_OK)]
            if len(candidates) != 1:
                raise ValueError('Expected one Mac shaderc executable after host-tools build; specify --shaderc explicitly.')
            shaderc = candidates[0]
        if not shaderc.is_file() or not os.access(shaderc, os.X_OK):
            raise ValueError('--shaderc must name an executable built for this Mac.')
    except (ValueError, OSError, RuntimeError, subprocess.SubprocessError) as error:
        build_attempt(operation, 'host_tools', 'failed', error=str(error))
        return blocked_operation(operation, 'host_tools_unavailable', str(error), 'Resolve host compiler prerequisites and start build again; no native player compilation has begun.')
    key = native_cache_key(args, manifest, report, shaderc)
    work = scoped(CACHE / 'native' / key)
    work.mkdir(parents=True, exist_ok=True)
    operation.update(status='building', toolchain=report, build_directory=str(work), native_cache_key=key, shaderc_sha256=digest(shaderc))
    save_operation(operation)
    lock = work / '.langmake-build-lock'
    try:
        descriptor = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError:
        return blocked_operation(operation, 'native_cache_busy', 'Another operation owns this native build cache.',
                                 'Wait for the active build. If it was interrupted, confirm its process has ended before removing ' + str(lock))
    try:
        with os.fdopen(descriptor, 'w') as stream: stream.write(operation['operation_id'])
        # Clear the previous bundle's resources without removing compiled objects.
        # Xcode does not remove resources dropped from a reconfigured target.
        for old in list(work.rglob('*.app')):
            if old.is_dir() and (old / 'langmake/manifest.json').is_file():
                scoped(old).relative_to(work)
                shutil.rmtree(old)
        for phase, command in [('configure', build_command(args, manifest, work, shaderc)),
                               ('compile_and_sign', native_build_command(args, manifest, work, selected))]:
            result = run(command, check=False)
            diagnostic = ((result.stdout or b'') + (result.stderr or b'')).decode('utf-8', 'replace')
            log = operation_path(operation['operation_id']).parent / (phase + '.log')
            log.write_bytes(diagnostic.encode('utf-8'))
            build_attempt(operation, phase, 'failed' if result.returncode else 'complete', returncode=result.returncode, log=str(log))
            if result.returncode:
                matches = [path for path in work.rglob('*.app') if path.is_dir() and (path / 'langmake/manifest.json').is_file()]
                recoverable = False
                if phase == 'compile_and_sign' and args.destination == 'device' and len(matches) == 1:
                    try:
                        verify_app(matches[0], manifest, args.arch, False)
                        recoverable = signing_recovery(operation, matches[0], diagnostic)
                    except (ValueError, OSError, RuntimeError):
                        pass
                if recoverable:
                    return blocked_operation(operation, 'signing_interaction_required', diagnostic[-4000:],
                        'Compilation produced a verified app. Run ' + operation['helper'] + ' locally on the Mac and approve any Keychain prompt; inspect the log if it still fails.', True)
                code = 'native_build_failed'
                for token, value in [('license', 'apple_agreement_required'), ('No signing certificate', 'signing_identity_unavailable'),
                                     ('requires a provisioning profile', 'provisioning_required'), ('SDK cannot be located', 'missing_sdk')]:
                    if token.lower() in diagnostic.lower(): code = value; break
                return blocked_operation(operation, code, diagnostic[-4000:],
                    'Resolve the reported prerequisite and start build again. Original evidence and the native compilation cache are retained.')
        matches = [path for path in work.rglob('*.app') if path.is_dir() and (path / 'langmake/manifest.json').is_file()]
        if len(matches) != 1:
            raise ValueError('Native build must produce exactly one Langmake .app bundle.')
        return finish_build(operation, matches[0], manifest)
    except (ValueError, OSError, RuntimeError, subprocess.SubprocessError) as error:
        build_attempt(operation, 'verification', 'failed', error=str(error))
        return blocked_operation(operation, 'artifact_verification_failed', str(error), 'Inspect operation evidence and correct the build; unverified artifacts are not exported.')
    finally:
        lock.unlink()


def simctl(arguments):
    require_mac()
    (CACHE / 'simulators').mkdir(parents=True, exist_ok=True)
    return ['xcrun', 'simctl', '--set', str(CACHE / 'simulators')] + list(arguments)


def apple_help(command, tokens=()):
    key = tuple(command)
    if key not in TOOL_HELP:
        result = run(['xcrun'] + list(command) + ['--help'], timeout=30)
        TOOL_HELP[key] = ((result.stdout or b'') + (result.stderr or b'')).decode('utf-8', 'replace')
    missing = [token for token in tokens if token not in TOOL_HELP[key]]
    if missing:
        raise ValueError('Installed Xcode command lacks required options %s. Inspect xcrun %s --help; this Xcode version needs qualification.' % (', '.join(missing), ' '.join(command)))


def device_json(arguments, check=True):
    require_mac()
    folder = scoped(CACHE / 'responses')
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / (uuid.uuid4().hex + '.json')
    # Global options must precede launch's remaining app arguments.
    try:
        result = run(['xcrun', 'devicectl', '--json-output', path] + list(arguments), check=check, timeout=120)
        data = read_json(path) if path.is_file() else {}
        if check and data.get('info', {}).get('outcome') not in (None, 'success'):
            raise RuntimeError('Apple device operation failed: ' + json.dumps(data.get('error', data)))
        return data, result
    finally:
        if path.is_file(): path.unlink()  # Transport responses are returned, not retained per poll.


def device_record(item):
    """Normalize both legacy and Xcode 27 records without mistaking pairing for hardware."""
    properties = item.get('properties') or {}
    legacy_hardware = item.get('hardwareProperties') or {}
    legacy_device = item.get('deviceProperties') or {}
    legacy_connection = item.get('connectionProperties') or {}
    hardware = dict(legacy_hardware, **(properties.get('hardware') or {}))
    connection = dict(legacy_connection, **(properties.get('connection') or {}))
    state = properties.get('state') or {}
    software = properties.get('software') or {}
    realities = [str(value).lower() for value in
                 [legacy_hardware.get('reality'), (properties.get('hardware') or {}).get('reality')] if value]
    providers = [legacy_device.get('provider'), properties.get('provider'), state.get('provider')]
    visibility = [item.get('visibilityClass'), state.get('visibilityClass')]
    simulated = ('simulated' in realities or 'simulator' in realities or
                 any('coresimulator' in str(value).lower() for value in providers if value) or
                 any(str(value).lower() == 'simulators' for value in visibility if value))
    reality = 'simulated' if simulated else ('physical' if 'physical' in realities else 'unknown')
    target_platform = hardware.get('platform')
    ios_platform = str(target_platform).lower() in ('ios', 'ipados')
    connection_state = connection.get('state', connection.get('tunnelState'))
    normalized_state = str(connection_state).lower()
    reachable = (True if normalized_state in ('connected', 'available') else
                 False if normalized_state in ('disconnected', 'unavailable') else None)
    pairing = connection.get('pairingState')
    os_version = software.get('osVersionNumber', legacy_device.get('osVersionNumber'))
    if isinstance(os_version, dict):
        os_version = os_version.get('stringValue')
    developer_mode = None
    for value in [state.get('developerModeStatus'), legacy_device.get('developerModeStatus')]:
        if isinstance(value, str) and value in ('enabled', 'disabled'):
            developer_mode = value
        elif isinstance(value, dict) and len(value) == 1:
            # Xcode 27 emits enum marker objects, e.g. {"enabled": {"mode": 1}}.
            # The marker names are meaningful; their integer payloads are not inferred.
            for marker in ('enabled', 'disabled'):
                if marker in value and isinstance(value[marker], dict):
                    developer_mode = marker
        if developer_mode is not None:
            break
    reason = ('simulated_device' if reality == 'simulated' else
              'unverified_reality' if reality != 'physical' else
              'unsupported_platform' if not ios_platform else None)
    return {'id': item.get('identifier'), 'udid': hardware.get('udid'), 'name': state.get('name', legacy_device.get('name')),
            'state': connection_state, 'pairing_state': pairing, 'os_version': os_version,
            'platform': target_platform, 'reality': reality, 'reachable': reachable,
            'available': reason is None and pairing == 'paired' and reachable is True,
            'developer_mode': developer_mode,
            'exclusion_reason': reason}


def physical_device_records():
    data, _ = device_json(['list', 'devices'])
    return [device_record(item) for item in data.get('result', {}).get('devices', [])]


def require_physical_device(args):
    """Check explicit device targets before any install, process or file operation."""
    identifier = selected_device(args)
    matches = [item for item in physical_device_records() if item['id'] == identifier]
    if len(matches) != 1:
        raise ValueError('Selected physical device is not uniquely present in devicectl. Run devices --destination device and select its returned id; connect and pair the iPhone or iPad if missing.')
    record = matches[0]
    if record['reality'] == 'simulated':
        raise ValueError('Selected target is simulated, not a physical iPhone/iPad. Use --destination simulator and an explicit simulator from the workspace device set.')
    if record['reality'] != 'physical':
        raise ValueError('Cannot verify that this target is physical: Xcode did not report hardware reality. Pair the iPhone/iPad and rerun devices --destination device; this Xcode device schema needs qualification if reality remains missing.')
    if record['exclusion_reason']:
        raise ValueError('Selected physical target is not a supported iOS/iPadOS device (reported platform: %s). Select an iPhone or iPad from devices --destination device.' % record['platform'])
    return record


def devices(args):
    require_mac()
    if args.destination == 'simulator':
        result = run(simctl(['list', 'devices', 'available', '--json']))
        data = json.loads(result.stdout)
        devices = []
        for runtime, entries in data.get('devices', {}).items():
            devices.extend({'id': item['udid'], 'name': item.get('name'), 'state': item.get('state'),
                            'os_version': runtime, 'available': item.get('isAvailable', False),
                            'reality': 'simulated', 'reachable': item.get('state') == 'Booted'} for item in entries)
    else:
        records = physical_device_records()
        return {'ok': True, 'destination': args.destination,
                'devices': [item for item in records if item['exclusion_reason'] is None],
                'excluded_devices': [item for item in records if item['exclusion_reason'] is not None]}
    return {'ok': True, 'destination': args.destination, 'devices': devices}


def selected_device(args):
    if not DEVICE.fullmatch(args.device or ''):
        raise ValueError('Select an explicit device identifier from devices; aliases such as booted/all are not accepted.')
    return args.device


def bridge_path(storage_id, relative):
    if not STORAGE.fullmatch(storage_id or ''):
        raise ValueError('Use the 24-character storage_id from stage/build output.')
    return 'Library/Application Support/Langmake/' + storage_id + '/' + relative_path(relative)


def install_receipt(args):
    name = hashlib.sha256((args.destination + '/' + selected_device(args) + '/' + bundle_id(args.bundle_id)).encode()).hexdigest()
    return scoped(CACHE / 'installed' / (name + '.json'))


def require_development_install(args):
    path = install_receipt(args)
    if not path.is_file() or not read_json(path).get('development'):
        raise ValueError('Private inspection/control requires a debug app installed with this tool. Install the debug .app first; release app access is intentionally unavailable.')


def simulator_container(args, kind='data'):
    result = run(simctl(['get_app_container', selected_device(args), bundle_id(args.bundle_id), kind]))
    # Simulator device storage is selected under this workspace, never the human's default set.
    path = scoped(result.stdout.decode('utf-8').strip())
    if not path.is_dir():
        raise ValueError('Simulator returned a nonexistent app container.')
    return path


def install(args):
    require_mac()
    device = selected_device(args)
    if args.destination == 'device': require_physical_device(args)
    app = scoped(args.app)
    if app.suffix != '.app' or not app.is_dir():
        raise ValueError('--app must select a built .app directory.')
    manifest = verify_stage(app / 'langmake')
    with (app / 'Info.plist').open('rb') as stream:
        info = plistlib.load(stream)
    identifier = bundle_id(info.get('CFBundleIdentifier'))
    if identifier != manifest['bundle_identifier']:
        raise ValueError('App bundle and content identifiers differ.')
    platforms = info.get('CFBundleSupportedPlatforms', [])
    expected = 'iPhoneOS' if args.destination == 'device' else 'iPhoneSimulator'
    if expected not in platforms:
        raise ValueError('The app was not built for the selected destination: ' + expected)
    if args.destination == 'device':
        run(['codesign', '--verify', '--deep', '--strict', app])
        apple_help(['devicectl', 'device', 'install', 'app'], ['--device'])
        device_json(['device', 'install', 'app', '--device', device, str(app)])
    else:
        run(simctl(['install', device, str(app)]))
    args.bundle_id = identifier
    report = {'ok': True, 'device': device, 'destination': args.destination, 'bundle_id': identifier,
              'development': manifest['development'], 'storage_id': manifest['storage_id'],
              'content_id': manifest['content_id'], 'app': str(app), 'installed_at_ms': time.time_ns() // 1000000}
    write_json(install_receipt(args), report)
    return report


def launch_arguments(args):
    flags = []
    for name in ['development', 'fresh', 'no_audio']:
        if getattr(args, name, False):
            flags.append('--' + name.replace('_', '-'))
    frames = getattr(args, 'frames', None)
    steps = getattr(args, 'steps', None)
    capture = getattr(args, 'capture', None)
    timeline = [getattr(args, field, None) for field in ['input', 'ticks', 'output_dir']]
    if frames is not None:
        if isinstance(frames, bool) or not 1 <= frames <= 36000:
            raise ValueError('--frames must be in 1..36000.')
        flags += ['--frames', str(frames)]
    if capture:
        if frames is None:
            raise ValueError('--capture requires bounded --frames; use a native capture control request for a live session.')
        flags += ['--capture', relative_path(capture)]
    if steps is not None or any(value is not None for value in timeline):
        if frames is not None or capture:
            raise ValueError('Frame capture and fixed-tick timeline capture cannot be mixed.')
        if steps is None or not all(value is not None for value in timeline) or not 0 <= steps <= 36000:
            raise ValueError('Timeline capture requires --steps 0..36000, --input, --ticks and --output-dir together.')
        tick_text = str(args.ticks)
        if not re.fullmatch(r'[0-9]+(?:,[0-9]+)*', tick_text):
            raise ValueError('Capture ticks must be comma-separated nonnegative integers.')
        ticks = [int(value) for value in tick_text.split(',')]
        if len(ticks) > 128 or ticks != sorted(set(ticks)) or ticks[-1] > steps:
            raise ValueError('Capture ticks must be unique, sorted, within --steps and at most 128 entries.')
        flags += ['--steps', str(steps), '--input', relative_path(args.input), '--ticks', tick_text,
                  '--output-dir', relative_path(args.output_dir)]
    return flags


def launch(args):
    require_mac()
    device, identifier = selected_device(args), bundle_id(args.bundle_id)
    if args.destination == 'device': require_physical_device(args)
    flags = launch_arguments(args)
    if flags:
        require_development_install(args)
    if args.destination == 'simulator':
        result = run(simctl(['launch', '--terminate-running-process', device, identifier] + flags))
        match = re.search(r':\s*(\d+)\s*$', result.stdout.decode('utf-8', 'replace'))
        pid = int(match.group(1)) if match else None
        raw = result.stdout.decode('utf-8', 'replace').strip()
    else:
        apple_help(['devicectl', 'device', 'process', 'launch'], ['--device', '--terminate-existing'] + (['arguments'] if flags else []))
        # devicectl forwards trailing arguments after the bundle identifier.
        # A literal '--' is forwarded too and prevents native launch options
        # from taking effect on the qualified physical-device workflow.
        raw, _ = device_json(['device', 'process', 'launch', '--device', device, '--terminate-existing', identifier] + flags)
        pid = raw.get('result', {}).get('process', {}).get('processIdentifier')
        if pid is None:
            pid = raw.get('result', {}).get('processIdentifier')
    report = {'ok': True, 'device': device, 'destination': args.destination, 'bundle_id': identifier,
              'pid': pid, 'arguments': flags, 'transport': raw,
              'note': 'Launch success is not proof of game initialization; inspect launch-result.json/live.json.'}
    write_json(CACHE / 'launches' / (uuid.uuid4().hex + '.json'), report)
    return report


def stop(args):
    require_mac()
    device = selected_device(args)
    if args.destination == 'device': require_physical_device(args)
    if args.destination == 'simulator':
        run(simctl(['terminate', device, bundle_id(args.bundle_id)]))
    else:
        if args.pid is None or args.pid <= 0:
            raise ValueError('Physical-device stop requires --pid from the current launch report. Do not guess a process ID.')
        # Use the installed primary command reference, never a guessed fallback
        # that could target another process. Xcode exposes signal on current builds.
        apple_help(['devicectl', 'device', 'process', 'signal'], ['--device', '--pid', '--signal'])
        device_json(['device', 'process', 'signal', '--device', device, '--pid', str(args.pid), '--signal', 'SIGTERM'])
    return {'ok': True, 'device': device, 'destination': args.destination}


def transfer(args, upload, relative, local):
    require_mac()
    if args.destination == 'device': require_physical_device(args)
    require_development_install(args)
    device, identifier = selected_device(args), bundle_id(args.bundle_id)
    remote = bridge_path(args.storage_id, relative)
    local = scoped(local)
    if upload and (not local.is_file() or local.stat().st_size > LIMIT):
        raise ValueError('Upload must be a bounded regular file.')
    if args.destination == 'simulator':
        container = simulator_container(args)
        target = (container / remote).resolve()
        target.relative_to(container)
        if upload:
            target.parent.mkdir(parents=True, exist_ok=True)
            temporary = target.with_name(target.name + '.upload-' + uuid.uuid4().hex)
            shutil.copyfile(local, temporary)
            temporary.replace(target)
        else:
            if not target.exists():
                raise AppFileNotFoundError(relative, 'simulator_container_lookup')
            if not target.is_file() or target.stat().st_size > LIMIT:
                raise ValueError('Requested app file is not regular or exceeds the transfer budget.')
            local.parent.mkdir(parents=True, exist_ok=True)
            temporary = local.with_name(local.name + '.pull-' + uuid.uuid4().hex)
            try:
                shutil.copyfile(target, temporary)
                temporary.replace(local)
            finally:
                if temporary.is_file(): temporary.unlink()
    else:
        action = 'to' if upload else 'from'
        apple_help(['devicectl', 'device', 'copy', action], ['--device', '--domain-type', '--domain-identifier', '--source', '--destination'])
        local.parent.mkdir(parents=True, exist_ok=True)
        temporary = local if upload else local.with_name(local.name + '.pull-' + uuid.uuid4().hex)
        arguments = ['device', 'copy', action, '--device', device, '--domain-type', 'appDataContainer',
                     '--domain-identifier', identifier, '--source', str(local) if upload else remote,
                     '--destination', remote if upload else str(temporary)]
        if upload:
            device_json(arguments)
        else:
            try:
                # Read the structured response even when devicectl exits nonzero.
                data, process = device_json(arguments, check=False)
                if not isinstance(data, dict) or not isinstance(data.get('info', {}), dict):
                    raise RuntimeError('Device copy returned unsupported structured output.')
                if process.returncode or data.get('info', {}).get('outcome') != 'success':
                    error = data.get('error', {})
                    if not isinstance(error, dict): error = {'details': error}
                    if error.get('domain') == 'com.apple.dt.CoreDeviceError' and error.get('code') == 7000:
                        try:
                            absent = device_file_absent(device, identifier, remote)
                        except (ValueError, OSError, RuntimeError, subprocess.SubprocessError) as verification:
                            raise RuntimeError('Device copy failed; file absence could not be verified: ' + str(verification) +
                                               '; copy error: ' + json.dumps(error)) from verification
                        if absent:
                            raise AppFileNotFoundError(relative, 'successful_parent_directory_listing')
                    diagnostic = ((process.stderr or b'') + (process.stdout or b'')).decode('utf-8', 'replace')[-4000:]
                    raise RuntimeError('Device copy failed: ' + json.dumps(error or data) + '\n' + diagnostic)
                if temporary.is_symlink() or not temporary.is_file() or temporary.stat().st_size > LIMIT:
                    raise ValueError('Device copy did not return a bounded regular file at the requested output path.')
                temporary.replace(local)
            finally:
                if temporary.is_file() or temporary.is_symlink(): temporary.unlink()
    return {'ok': True, 'device': device, 'path': relative, 'local': str(local), 'bytes': local.stat().st_size}


def device_file_absent(device, identifier, remote):
    """Confirm a failed copy's target is absent from a complete, successful listing."""
    apple_help(['devicectl', 'device', 'info', 'files'],
               ['--device', '--domain-type', '--domain-identifier', '--subdirectory', '--no-recurse'])
    path = PurePosixPath(remote)
    data, process = device_json(['device', 'info', 'files', '--device', device,
        '--domain-type', 'appDataContainer', '--domain-identifier', identifier,
        '--subdirectory', str(path.parent), '--no-recurse'], check=False)
    if not isinstance(data, dict) or not isinstance(data.get('info'), dict) or not isinstance(data.get('result'), dict):
        raise RuntimeError('App directory listing returned unsupported structured output.')
    result = data.get('result', {})
    if process.returncode or data.get('info', {}).get('outcome') != 'success' or \
            data.get('info', {}).get('commandType') != 'devicectl.device.info.files' or \
            result.get('deviceIdentifier') != device or result.get('domain') != 'appDataContainer' or \
            result.get('domainIdentifier') != identifier:
        raise RuntimeError('App directory listing failed or returned unverified scope: ' + json.dumps(data))
    files = result.get('files')
    if not isinstance(files, list) or len(files) > FILE_LIMIT:
        raise RuntimeError('App directory listing did not return a bounded complete file list.')
    names = set()
    for item in files:
        name = item.get('name') if isinstance(item, dict) else None
        if not isinstance(name, str) or not name or name in ('.', '..') or '/' in name or '\\' in name or \
                item.get('relativePath') != name or name.casefold() in names:
            raise RuntimeError('App directory listing contains unsupported or ambiguous entries.')
        names.add(name.casefold())
    return path.name.casefold() not in names


def bridge(args):
    if args.command == 'inspect':
        key = hashlib.sha256((args.destination + '/' + selected_device(args) + '/' + bundle_id(args.bundle_id) + '/' + args.storage_id).encode()).hexdigest()
        local = scoped(CACHE / 'observations' / (key + '.json'))
        transfer(args, False, 'live.json', local)
        return {'ok': True, 'device': args.device, 'live': read_json(local), 'observation_file': str(local)}
    if args.command == 'control':
        request_path = scoped(args.file)
        request = read_json(request_path, 16384)
        identifier = request.get('id')
        if not isinstance(identifier, str) or not re.fullmatch(r'[0-9]{1,96}', identifier):
            raise ValueError('Control id must be a numeric string of 1–96 digits.')
        if not isinstance(request.get('session'), str) or not request['session'] or not isinstance(request.get('action'), str):
            raise ValueError('Control requires the current native session and an action.')
        # devicectl file copies are not atomic. Commit only after copying the full
        # request. Native verifies its hash and atomically renames it to commands/.
        report = transfer(args, True, 'inbox/' + identifier + '.json', request_path)
        ready = scoped(CACHE / 'requests' / (uuid.uuid4().hex + '.ready'))
        ready.parent.mkdir(parents=True, exist_ok=True)
        ready.write_text(digest(request_path) + '\n', encoding='ascii')
        try:
            transfer(args, True, 'inbox/' + identifier + '.ready', ready)
        finally:
            ready.unlink()
        report.update({'request_id': identifier, 'executed': None, 'receipt_path': 'results/' + identifier + '.json',
                       'note': 'Uploaded only. Poll the receipt; never resend a mutation with a new ID merely because delivery is delayed.'})
        return report
    if args.command == 'push' and args.path.startswith(('commands/', 'inbox/')):
        raise ValueError('Use control to upload commands through the atomic commit protocol.')
    if args.command == 'push' and args.destination == 'device' and '/' in args.path:
        raise ValueError('Physical-device push currently accepts a filename at private storage root. Nested directory creation requires separate device qualification.')
    return transfer(args, args.command == 'push', args.path, args.file if args.command == 'push' else args.output)


def simulator(args):
    require_mac()
    if args.command == 'simulator-list':
        runtimes = json.loads(run(simctl(['list', 'runtimes', '--json'])).stdout)
        types = json.loads(run(simctl(['list', 'devicetypes', '--json'])).stdout)
        return {'ok': True, 'runtimes': runtimes.get('runtimes', []), 'device_types': types.get('devicetypes', [])}
    if args.command == 'simulator-create':
        if not re.fullmatch(r'com\.apple\.CoreSimulator\.SimDeviceType\.[A-Za-z0-9-]+', args.device_type or '') or not re.fullmatch(r'com\.apple\.CoreSimulator\.SimRuntime\.iOS-[0-9-]+', args.runtime or ''):
            raise ValueError('Select explicit iOS runtime and device-type identifiers from simulator-list.')
        result = run(simctl(['create', args.name, args.device_type, args.runtime]))
        return {'ok': True, 'device': result.stdout.decode('utf-8').strip(), 'device_set': str(CACHE / 'simulators')}
    device = selected_device(args)
    run(simctl(['boot', device]))
    run(simctl(['bootstatus', device, '-b']), timeout=180)
    return {'ok': True, 'device': device, 'state': 'Booted'}


def screenshot(args):
    require_mac()
    if args.destination != 'simulator':
        raise ValueError('Physical-device screenshots use the native debug capture command and pull. devicectl has no qualified screen-capture command here.')
    output = scoped(args.output)
    if output.exists():
        raise ValueError('Screenshot output already exists.')
    output.parent.mkdir(parents=True, exist_ok=True)
    run(simctl(['io', selected_device(args), 'screenshot', '--type=png', output]))
    if output.read_bytes()[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('Simulator screenshot did not produce PNG bytes.')
    return {'ok': True, 'output': str(output), 'exact_tick': False}


def parser():
    result = argparse.ArgumentParser(description=__doc__)
    commands = result.add_subparsers(dest='command', required=True)
    commands.add_parser('doctor')
    stage_parser = commands.add_parser('stage')
    for name in ['project', 'output']:
        stage_parser.add_argument('--' + name, required=True)
    for name in ['bundle-id', 'label', 'version', 'icon', 'profile']:
        stage_parser.add_argument('--' + name)
    stage_parser.add_argument('--build-number', type=int)
    stage_parser.add_argument('--host-build')
    stage_parser.add_argument('--orientation', choices=['landscape', 'portrait', 'sensor'])
    stage_parser.add_argument('--configuration', choices=['debug', 'release'], default='debug')
    verify = commands.add_parser('verify-stage'); verify.add_argument('--staged', required=True)
    build_parser = commands.add_parser('build')
    build_parser.add_argument('--staged', required=True)
    build_parser.add_argument('--destination', choices=['device', 'simulator'], required=True)
    build_parser.add_argument('--arch', choices=['arm64', 'x86_64'], default='arm64')
    for name in ['team-id', 'signing-identity', 'provisioning-profile', 'shaderc', 'output']:
        build_parser.add_argument('--' + name)
    build_parser.add_argument('--allow-provisioning-updates', action='store_true')
    build_parser.add_argument('--device', help='Explicit physical CoreDevice id from devices; use its verified hardware UDID for the Xcode scheme destination.')
    build_parser.add_argument('--register-device', action='store_true', help='Allow Xcode to register this physical device with the selected team; requires --device and --allow-provisioning-updates.')
    build_parser.add_argument('--jobs', type=int, choices=range(1, 65), default=4)
    for name in ['operation', 'resume']:
        operation_parser = commands.add_parser(name)
        operation_parser.add_argument('--operation', required=True)
        if name == 'operation': operation_parser.add_argument('--full', action='store_true')
    list_parser = commands.add_parser('devices')
    list_parser.add_argument('--destination', choices=['device', 'simulator'], required=True)
    for name in ['install', 'launch', 'stop', 'inspect', 'push', 'pull', 'control', 'screenshot']:
        sub = commands.add_parser(name)
        sub.add_argument('--destination', choices=['device', 'simulator'], required=True)
        sub.add_argument('--device', required=True)
        if name not in ['install', 'screenshot']:
            sub.add_argument('--bundle-id', required=True)
        if name in ['inspect', 'push', 'pull', 'control']:
            sub.add_argument('--storage-id', required=True)
        if name == 'install': sub.add_argument('--app', required=True)
        if name in ['push', 'control']: sub.add_argument('--file', required=True)
        if name in ['push', 'pull']: sub.add_argument('--path', required=True)
        if name in ['pull', 'screenshot']: sub.add_argument('--output', required=True)
        if name == 'stop': sub.add_argument('--pid', type=int)
        if name == 'launch':
            for flag in ['development', 'fresh', 'no-audio']: sub.add_argument('--' + flag, action='store_true')
            for flag in ['frames', 'steps']: sub.add_argument('--' + flag, type=int)
            for flag in ['capture', 'input', 'ticks', 'output-dir']: sub.add_argument('--' + flag)
    commands.add_parser('simulator-list')
    create = commands.add_parser('simulator-create')
    create.add_argument('--name', required=True)
    create.add_argument('--device-type', required=True)
    create.add_argument('--runtime', required=True)
    boot = commands.add_parser('simulator-boot'); boot.add_argument('--device', required=True)
    return result


def main(argv=None):
    args = parser().parse_args(argv)
    try:
        if args.command == 'doctor': result = doctor(args)
        elif args.command == 'stage': result = stage(args)
        elif args.command == 'verify-stage': result = {'ok': True, 'manifest': verify_stage(args.staged)}
        elif args.command == 'build': result = build(args)
        elif args.command == 'operation': result = inspect_operation(args)
        elif args.command == 'resume': result = resume(args)
        elif args.command == 'devices': result = devices(args)
        elif args.command == 'install': result = install(args)
        elif args.command == 'launch': result = launch(args)
        elif args.command == 'stop': result = stop(args)
        elif args.command in ['inspect', 'push', 'pull', 'control']: result = bridge(args)
        elif args.command == 'screenshot': result = screenshot(args)
        else: result = simulator(args)
    except (ValueError, KeyError, TypeError, OSError, RuntimeError, subprocess.SubprocessError) as error:
        result = {'ok': False, 'command': args.command, 'error': str(error)}
        if isinstance(error, AppFileNotFoundError):
            result.update({'code': 'file_not_found', 'path': error.path, 'evidence': error.evidence})
    # Escaped non-ASCII keeps JSON usable in Windows consoles with legacy encoding.
    print(json.dumps(result, indent=2))
    return 0 if result.get('ok') else 1


if __name__ == '__main__':
    # Structured callers (Studio and external agents) decode UTF-8 on every host.
    for stream in [sys.stdout, sys.stderr]:
        if hasattr(stream, 'reconfigure'):
            stream.reconfigure(encoding='utf-8', errors='backslashreplace')
    sys.exit(main())
