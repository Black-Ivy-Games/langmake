"""Explicit, per-game local target binding. No inherited or scanned SSH defaults."""
import hashlib
import os
from pathlib import Path, PurePosixPath
import platform
import re
from .common import WorkflowError, identifier, read, write, within


def config_path(project):
    return within(project / '.langmake/targets.local.json', project)


def binding(project):
    return hashlib.sha256((platform.node() + '\0' + os.path.normcase(str(project.resolve()))).encode()).hexdigest()


def fingerprint(target):
    import json
    return hashlib.sha256(json.dumps(target, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode('utf-8')).hexdigest()


def empty(project):
    return {'format_version': 1, 'project_binding': binding(project), 'targets': {}}


def load(project):
    path = config_path(project)
    if not path.is_file():
        return empty(project)
    result = read(path)
    if not isinstance(result, dict) or result.get('format_version') != 1 or result.get('project_binding') != binding(project):
        raise WorkflowError('target_setup_required', 'This local target record belongs to another game directory or machine. Explicitly configure targets for this game.', config=str(path))
    if not isinstance(result.get('targets'), dict):
        raise WorkflowError('invalid_target_config', 'Local target list must be an object.')
    for name, target in result['targets'].items():
        identifier(name, 'target')
        validate(target)
    return result


def validate(value):
    if not isinstance(value, dict):
        raise WorkflowError('invalid_target', 'A target must be a JSON object.')
    allowed = {'platform','destination','device','serial','abi','configuration','profile','host',
               'team_id','allow_provisioning_updates','register_device','game_directory','application_id',
               'verification_application_id','orientation','prebuilt_player','role'}
    unknown = set(value) - allowed
    if unknown:
        raise WorkflowError('invalid_target', 'Unknown target fields: ' + ', '.join(sorted(unknown)))
    for name in allowed - {'host', 'allow_provisioning_updates', 'register_device'}:
        if name in value and (not isinstance(value[name], str) or not value[name] or len(value[name]) > 4096 or any(ord(c) < 32 for c in value[name])):
            raise WorkflowError('invalid_target', name + ' must be a nonempty string without control characters.')
    target_platform = value.get('platform')
    if target_platform not in ('windows', 'android', 'ios'):
        raise WorkflowError('invalid_target', 'Target platform must be windows, android or ios.')
    if value.get('configuration', 'debug') not in ('debug','release'):
        raise WorkflowError('invalid_target', 'configuration must be debug or release.')
    if value.get('role', 'human') not in ('human','verification'):
        raise WorkflowError('invalid_target', 'role must be human or verification.')
    if value.get('profile'):
        identifier(value['profile'], 'profile')
    for flag in ('allow_provisioning_updates','register_device'):
        if flag in value and type(value[flag]) is not bool:
            raise WorkflowError('invalid_target', flag + ' must be boolean.')
    for name in ('application_id','verification_application_id'):
        if name in value and not re.fullmatch(r'[A-Za-z][A-Za-z0-9-]*(?:\.[A-Za-z0-9][A-Za-z0-9-]*)+', value[name]):
            raise WorkflowError('invalid_target', name + ' must be a reverse-DNS application identifier.')
    if value.get('orientation','landscape') not in ('landscape','portrait','sensor'):
        raise WorkflowError('invalid_target', 'Unsupported orientation.')
    if target_platform == 'ios':
        if value.get('destination') not in ('device','simulator'):
            raise WorkflowError('invalid_target', 'iOS requires destination device or simulator.')
        if not re.fullmatch(r'[A-Fa-f0-9-]{8,64}', value.get('device','')):
            raise WorkflowError('device_required', 'Select an explicit iOS device ID; names and implicit defaults are not supported.')
        if value.get('destination') == 'device' and not re.fullmatch(r'[A-Z0-9]{10}', value.get('team_id','')):
            raise WorkflowError('signing_team_required', 'Physical iOS targets require the game\'s explicitly selected 10-character team ID.')
        if value.get('register_device') and not value.get('allow_provisioning_updates'):
            raise WorkflowError('invalid_target', 'register_device requires allow_provisioning_updates.')
        if value['destination'] == 'simulator' and any(value.get(k) for k in ('team_id', 'register_device', 'allow_provisioning_updates')):
            raise WorkflowError('invalid_target', 'Signing and device registration apply only to physical iOS devices.')
        if value.get('abi', 'arm64') not in ('arm64', 'x86_64') or (value['destination'] == 'device' and value.get('abi', 'arm64') != 'arm64'):
            raise WorkflowError('invalid_target', 'Physical iOS devices require arm64; simulators support arm64 or x86_64.')
    if target_platform == 'android':
        if not re.fullmatch(r'[A-Za-z0-9_.:-]{1,128}', value.get('serial','')):
            raise WorkflowError('device_required', 'Android requires an explicit serial.')
        if value.get('abi') not in ('arm64-v8a','x86_64'):
            raise WorkflowError('invalid_target', 'Select Android ABI arm64-v8a or x86_64.')
    host = value.get('host', {'transport':'local'})
    if not isinstance(host, dict) or set(host) - {'transport','alias','config','workspace','python','environment_script'}:
        raise WorkflowError('invalid_target', 'Invalid build-host fields.')
    for name, item in host.items():
        if not isinstance(item, str) or not item or len(item) > 4096 or any(ord(c) < 32 for c in item):
            raise WorkflowError('invalid_target', 'Host ' + name + ' must be a nonempty string without control characters.')
    if host.get('transport') == 'ssh':
        if target_platform != 'ios':
            raise WorkflowError('invalid_target', 'The initial remote build adapter supports iOS Mac hosts.')
        if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,127}', host.get('alias','')):
            raise WorkflowError('invalid_target', 'Select a named SSH host alias; raw command fragments are not accepted.')
        if not isinstance(host.get('config'), str) or not host['config']:
            raise WorkflowError('ssh_config_required', 'Explicitly select an SSH configuration file for this game.')
        workspace = host.get('workspace', '')
        if not isinstance(workspace, str) or not workspace.startswith('/') or '..' in PurePosixPath(workspace).parts or '\x00' in workspace:
            raise WorkflowError('invalid_target', 'Remote workspace must be an absolute Mac path without traversal.')
        game = value.get('game_directory', '')
        if not isinstance(game, str) or not game or game.startswith('/') or any(p in ('','..','.') for p in game.split('/')) or '\\' in game:
            raise WorkflowError('invalid_target', 'Set this game\'s own game_directory relative to the selected remote workspace.')
        for name in ('python','environment_script'):
            item = host.get(name)
            if item is not None and (not isinstance(item,str) or not item or '\x00' in item or '\n' in item or '\r' in item):
                raise WorkflowError('invalid_target', 'Invalid host ' + name)
        if host.get('environment_script'):
            path = PurePosixPath(host['environment_script'])
            if path.is_absolute() or '..' in path.parts:
                raise WorkflowError('invalid_target', 'environment_script must stay within the selected remote workspace.')
        if workspace == '/' or ':' in game:
            raise WorkflowError('invalid_target', 'Select a dedicated remote workspace and portable game directory.')
    elif host.get('transport') != 'local':
        raise WorkflowError('invalid_target', 'Build-host transport must be local or ssh.')
    elif set(host) != {'transport'}:
        raise WorkflowError('invalid_target', 'Local hosts accept only transport; SSH settings must be explicitly selected for SSH.')
    return value


def add(project, name, value):
    identifier(name, 'target')
    validate(value)
    current = load(project)
    if name in current['targets']:
        raise WorkflowError('target_exists', 'Remove the existing target explicitly before replacing it.')
    current['targets'][name] = value
    write(config_path(project), current)
    return {'ok': True, 'target': name, 'config': str(config_path(project)), 'scope': 'this game on this machine',
            'note': 'Target configuration records a selection; deployment is a separate explicit command.'}


def remove(project, name):
    identifier(name, 'target')
    current = load(project)
    if name not in current['targets']:
        raise WorkflowError('target_missing', 'No configured target named ' + name)
    del current['targets'][name]
    write(config_path(project), current)
    return {'ok': True, 'removed': name}


def selected(project, name):
    identifier(name, 'target')
    current = load(project)
    if name not in current['targets']:
        raise WorkflowError('target_setup_required', 'Configure this target for this game using target add --file SETTINGS.json.', target=name)
    return current['targets'][name]
