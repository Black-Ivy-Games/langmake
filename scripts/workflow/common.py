"""Bounded files, native commands, and per-game operation records."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time
import uuid
from contextlib import ExitStack

ROOT = Path(__file__).resolve().parents[2]
FORMAT = 1
MAX_JSON = 32 * 1024 * 1024
NAME = re.compile(r'[A-Za-z][A-Za-z0-9_-]{0,63}')


class WorkflowError(Exception):
    def __init__(self, code, message, **details):
        super().__init__(message)
        self.code, self.details = code, details


def failure(error):
    return {'ok': False, 'code': getattr(error, 'code', 'operation_failed'),
            'message': str(error), **getattr(error, 'details', {})}


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for data in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(data)
    return h.hexdigest()


def read(path, limit=MAX_JSON):
    path = Path(path)
    with path.open('rb') as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise WorkflowError('file_too_large', 'JSON exceeds its bounded input limit: ' + str(path))
    return json.loads(data.decode('utf-8-sig'))


def write(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.new-' + uuid.uuid4().hex)
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    temporary.replace(path)


def within(path, root=ROOT):
    path, root = Path(path).resolve(), Path(root).resolve()
    try:
        path.relative_to(root)
    except ValueError:
        raise WorkflowError('path_outside_workspace', 'Path must stay in the selected workspace: ' + str(path))
    return path


def project_root(value):
    path = Path(value).resolve()
    if path.is_file():
        if path.name != 'project.json':
            raise WorkflowError('invalid_project', 'Select a game directory or its project.json.')
        path = path.parent
    path = within(path)
    document = read(path / 'project.json')
    if not isinstance(document, dict):
        raise WorkflowError('invalid_project', 'project.json must be an object.')
    return path


def local(project, kind):
    if not isinstance(kind, str) or not NAME.fullmatch(kind):
        raise WorkflowError('invalid_name', 'Invalid local record kind.')
    return within(project / '.langmake' / kind, project)


def identifier(value, what='name'):
    if not isinstance(value, str) or not NAME.fullmatch(value):
        raise WorkflowError('invalid_name', what + ' must match [A-Za-z][A-Za-z0-9_-]{0,63}.')
    return value


def runtime(value=None):
    candidates = [Path(value)] if value else ([Path(os.environ['LANGMAKE_ENGINE'])] if os.environ.get('LANGMAKE_ENGINE') else
                 [ROOT / 'build/langmake.exe', ROOT / 'langmake.exe', ROOT / 'build/langmake', ROOT / 'langmake'])
    for path in candidates:
        if path.is_file():
            return within(path)
    raise WorkflowError('runtime_missing', 'Select the current native runtime with --engine PATH; build it or use a current runtime package.')


def command(argv, folder, label, timeout=120, check=True, cwd=ROOT, environment=None, stdin=None):
    """No shell, no console state dumps; preserve full output in bounded-run files."""
    folder = within(folder)
    folder.mkdir(parents=True, exist_ok=True)
    label = identifier(label, 'log label')
    out, err = folder / (label + '.stdout.json'), folder / (label + '.stderr.log')
    if out.exists() or err.exists():
        label += '-' + uuid.uuid4().hex
        out, err = folder / (label + '.stdout.json'), folder / (label + '.stderr.log')
    env = os.environ.copy()
    env['PYTHONPYCACHEPREFIX'] = str(ROOT / '.cache/python')
    if environment:
        env.update(environment)
    try:
        with ExitStack() as resources:
            stdout = resources.enter_context(out.open('xb'))
            stderr = resources.enter_context(err.open('xb'))
            source = resources.enter_context(within(stdin).open('rb')) if stdin is not None else subprocess.DEVNULL
            process = subprocess.run([str(a) for a in argv], cwd=cwd, env=env, stdout=stdout, stderr=stderr,
                                     stdin=source, timeout=timeout, shell=False)
    except subprocess.TimeoutExpired as error:
        raise WorkflowError('command_timeout', 'Command timed out; it was not retried.',
                            stdout=str(out), stderr=str(err)) from error
    try:
        data = read(out)
    except (ValueError, OSError, WorkflowError):
        data = None
    result = {'exit_code': process.returncode, 'data': data, 'stdout': str(out), 'stderr': str(err)}
    if check and (process.returncode or not isinstance(data, dict) or data.get('ok') is False):
        raise WorkflowError('command_failed', 'Command failed; read the preserved output.',
                            command=str(argv[0]), result=result)
    return result


def native(engine, args, folder, label, timeout=120, check=True):
    return command([engine, *args], folder, label, timeout=timeout, check=check)


def record(project, kind):
    identity = uuid.uuid4().hex
    folder = local(project, kind) / identity
    folder.mkdir(parents=True, exist_ok=False)
    return identity, folder


def now():
    return time.time_ns() // 1000000


def profile_arguments(profile):
    return ['--profile', identifier(profile, 'profile')] if profile else []


def pointer(value, path):
    if path == '':
        return value
    if not isinstance(path, str) or not path.startswith('/'):
        raise WorkflowError('invalid_assertion', 'Assertion paths use JSON Pointer, for example /state/won.')
    for part in path[1:].split('/'):
        if re.search(r'~(?![01])', part):
            raise ValueError('Invalid JSON Pointer escape')
        part = part.replace('~1', '/').replace('~0', '~')
        if isinstance(value, list):
            if not re.fullmatch(r'0|[1-9][0-9]*', part):
                raise ValueError('Invalid JSON Pointer array index')
            value = value[int(part)]
        else:
            value = value[part]
    return value
