"""Apply game-owned icon/version resources to an explicitly copied Windows player.

No compiler or additional Python modules are required. The original engine file
must be provided so accidental in-place edits and hard links are rejected.
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import mobile_assets


def version_resource(application):
    def align(data): return data + b'\0' * (-len(data) % 4)
    def wide(text): return (text + '\0').encode('utf-16le')
    def block(key, value=b'', children=b'', text=False):
        header = struct.pack('<HHH', 0, len(value)//2 if text else len(value), int(text)) + wide(key)
        content = align(header) + value
        if children: content = align(content) + children
        return struct.pack('<H', len(content)) + content[2:]
    parts = [int(value) for value in application['version'].split('.')]
    parts += [0] * (3-len(parts))
    build = application['build']
    # File revision retains the full 32-bit build counter across its last two
    # numeric components; ProductVersion retains the authored semantic version.
    fixed = struct.pack('<13I', 0xFEEF04BD, 0x00010000,
                        (parts[0] << 16) | parts[1], build,
                        (parts[0] << 16) | parts[1], parts[2] << 16,
                        0x3f, 0, 0x40004, 1, 0, 0, 0)
    strings = {'FileDescription': application['label'], 'ProductName': application['label'],
               'FileVersion': application['version'] + '+' + str(build), 'ProductVersion': application['version'],
               'InternalName': application['id'], 'OriginalFilename': 'langmake.exe'}
    entries = b''.join(align(block(key, wide(value), text=True)) for key, value in strings.items())
    table = block('040904b0', children=entries, text=True)
    info = block('StringFileInfo', children=table, text=True)
    translation = block('Translation', struct.pack('<HH', 0x0409, 1200))
    variables = block('VarFileInfo', children=translation, text=True)
    return block('VS_VERSION_INFO', fixed, align(info) + variables)


def icon_resources(source):
    size, rgb = mobile_assets.decode_png(source, 256)
    entries, resources = [], []
    for identifier, pixels in enumerate([16, 24, 32, 48, 64, 128, 256], 1):
        data = mobile_assets.png_bytes(pixels, mobile_assets.resize(size, rgb, pixels))
        resources.append((3, identifier, data))  # RT_ICON
        entries.append(struct.pack('<BBBBHHIH', pixels if pixels < 256 else 0,
                                   pixels if pixels < 256 else 0, 0, 0, 1, 24, len(data), identifier))
    group = struct.pack('<HHH', 0, 1, len(entries)) + b''.join(entries)
    return resources + [(14, 1, group)]  # RT_GROUP_ICON


def kernel_api():
    if os.name != 'nt': raise ValueError('Windows executable resource export requires Windows; author/cook on any supported host.')
    api = ctypes.WinDLL('kernel32', use_last_error=True)
    api.BeginUpdateResourceW.argtypes = [wintypes.LPCWSTR, wintypes.BOOL]
    api.BeginUpdateResourceW.restype = wintypes.HANDLE
    api.UpdateResourceW.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, wintypes.WORD, ctypes.c_void_p, wintypes.DWORD]
    api.UpdateResourceW.restype = wintypes.BOOL
    api.EndUpdateResourceW.argtypes = [wintypes.HANDLE, wintypes.BOOL]
    api.EndUpdateResourceW.restype = wintypes.BOOL
    api.LoadLibraryExW.argtypes = [wintypes.LPCWSTR, wintypes.HANDLE, wintypes.DWORD]
    api.LoadLibraryExW.restype = wintypes.HANDLE
    api.FindResourceExW.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, wintypes.WORD]
    api.FindResourceExW.restype = wintypes.HANDLE
    api.LoadResource.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
    api.LoadResource.restype = wintypes.HANDLE
    api.LockResource.argtypes = [wintypes.HANDLE]
    api.LockResource.restype = ctypes.c_void_p
    api.SizeofResource.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
    api.SizeofResource.restype = wintypes.DWORD
    api.FreeLibrary.argtypes = [wintypes.HANDLE]
    api.FreeLibrary.restype = wintypes.BOOL
    return api


def update_resources(executable, resources):
    api = kernel_api()
    handle = api.BeginUpdateResourceW(str(executable), False)
    if not handle: raise ctypes.WinError(ctypes.get_last_error())
    try:
        for kind, identifier, data in resources:
            buffer = ctypes.create_string_buffer(data)
            if not api.UpdateResourceW(handle, kind, identifier, 0x0409, buffer, len(data)):
                raise ctypes.WinError(ctypes.get_last_error())
    except Exception:
        api.EndUpdateResourceW(handle, True)
        raise
    if not api.EndUpdateResourceW(handle, False): raise ctypes.WinError(ctypes.get_last_error())
    module = api.LoadLibraryExW(str(executable), None, 0x00000002 | 0x00000020)  # DATAFILE | IMAGE_RESOURCE, never execute code.
    if not module: raise ctypes.WinError(ctypes.get_last_error())
    evidence = []
    try:
        for kind, identifier, expected in resources:
            found = api.FindResourceExW(module, kind, identifier, 0x0409)
            if not found: raise ctypes.WinError(ctypes.get_last_error())
            size = api.SizeofResource(module, found)
            loaded = api.LoadResource(module, found)
            pointer = api.LockResource(loaded) if loaded else None
            if not pointer: raise ctypes.WinError(ctypes.get_last_error())
            actual = ctypes.string_at(pointer, size)
            if actual != expected: raise ValueError('Windows executable resource differs after export.')
            evidence.append({'type': kind, 'id': identifier, 'language': 0x0409, 'bytes': size,
                             'sha256': hashlib.sha256(actual).hexdigest()})
    finally:
        api.FreeLibrary(module)
    return evidence


def apply_application(project_file, executable, source_executable, overrides=None, configuration='debug'):
    project_file, executable, original = Path(project_file).resolve(), Path(executable).resolve(), Path(source_executable).resolve()
    if not executable.is_file() or not original.is_file() or executable.suffix.lower() != '.exe':
        raise ValueError('Select an existing copied .exe and its original engine executable.')
    if executable == original or os.path.samefile(executable, original):
        raise ValueError('Never modify the shared engine executable; copy it to a game export first.')
    project = json.loads(project_file.read_text(encoding='utf-8-sig'))
    # Legacy games without portable identity keep the existing player resources.
    if 'application' not in project and not (overrides or {}).get('id'):
        return {'ok': True, 'applied': False, 'reason': 'Legacy project has no application metadata.', 'executable': str(executable)}
    application = mobile_assets.metadata(project, 'windows', configuration, overrides)
    original_hash = mobile_assets.sha256(original)
    resources = [(16, 1, version_resource(application))]  # RT_VERSION
    icon = None
    if application.get('icon'):
        source = mobile_assets.icon_path(project_file.parent, application['icon'])
        resources += icon_resources(source)
        icon = {'source': application['icon'], 'source_sha256': mobile_assets.sha256(source)}
    evidence = update_resources(executable, resources)
    if mobile_assets.sha256(original) != original_hash:
        raise ValueError('Original engine changed during export; repeat with a stable source build.')
    return {'ok': True, 'applied': True, 'application': application, 'icon': icon, 'resources': evidence,
            'source_executable_sha256': original_hash, 'executable': str(executable),
            'executable_sha256': mobile_assets.sha256(executable), 'warnings': application['warnings']}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['apply'])
    parser.add_argument('--project', required=True, help='Cooked project.json file.')
    parser.add_argument('--executable', required=True, help='Game export copy to update.')
    parser.add_argument('--source-executable', required=True, help='Original engine file; must remain unchanged.')
    parser.add_argument('--configuration', choices=['debug', 'release'], default='debug')
    args = parser.parse_args(argv)
    try: report = apply_application(args.project, args.executable, args.source_executable, configuration=args.configuration)
    except (ValueError, OSError) as error: report = {'ok': False, 'error': str(error)}
    print(json.dumps(report, indent=2))
    return 0 if report['ok'] else 1


if __name__ == '__main__': sys.exit(main())
