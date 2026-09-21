"""Windows application resources. Native test edits a temporary copy only."""
import ctypes
from ctypes import wintypes
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('langmake_windows', Path(__file__).with_name('windows.py'))
windows = importlib.util.module_from_spec(spec)
spec.loader.exec_module(windows)
ROOT = Path(__file__).resolve().parents[2]


class WindowsExportTests(unittest.TestCase):
    def setUp(self):
        folder = ROOT / '.cache/windows-resource-tests'; folder.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=folder)
        self.root = Path(self.temporary.name)

    def tearDown(self): self.temporary.cleanup()

    def test_version_binary_has_correct_lengths_and_text(self):
        resource = windows.version_resource({'id': 'org.example.game', 'label': 'Rockets & Aliens', 'version': '1.2.3', 'build': 200000})
        self.assertEqual(struct.unpack_from('<H', resource)[0], len(resource))
        self.assertIn('Rockets & Aliens'.encode('utf-16le'), resource)
        self.assertIn('1.2.3+200000'.encode('utf-16le'), resource)

    def test_original_engine_and_hardlink_are_rejected(self):
        original = self.root / 'engine.exe'; original.write_bytes(b'fixture')
        with self.assertRaisesRegex(ValueError, 'shared engine'):
            windows.apply_application(self.root / 'project.json', original, original)
        linked = self.root / 'linked.exe'
        os.link(original, linked)
        with self.assertRaisesRegex(ValueError, 'shared engine'):
            windows.apply_application(self.root / 'project.json', linked, original)

    @unittest.skipUnless(os.name == 'nt', 'Win32 resource verification requires Windows')
    def test_actual_copied_player_icon_version_and_execution(self):
        candidates = [ROOT / 'build/langmake.exe', ROOT / '.cache/build-0.4/langmake.exe', ROOT / 'langmake.exe']
        original = next((path for path in candidates if path.is_file()), None)
        if original is None: self.skipTest('Build the Windows player before native resource verification.')
        copied = self.root / 'Game.exe'; shutil.copyfile(original, copied)
        for dll in original.parent.glob('*.dll'): shutil.copyfile(dll, self.root / dll.name)
        icon = self.root / 'icon.png'; icon.write_bytes(windows.mobile_assets.png_bytes(256, b'\x20\x70\xc0' * 256 * 256))
        project = self.root / 'project.json'
        project.write_text(json.dumps({'title': 'Resource fixture', 'application': {'id': 'org.example.resources',
                           'label': 'My Rocket Game', 'version': '2.3.4', 'build': 9, 'icon': 'icon.png'}}))
        before = windows.mobile_assets.sha256(original)
        report = windows.apply_application(project, copied, original, configuration='release')
        self.assertTrue(report['applied']); self.assertEqual(len(report['resources']), 9)
        self.assertEqual(before, windows.mobile_assets.sha256(original))
        api = ctypes.WinDLL('version', use_last_error=True)
        api.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(wintypes.DWORD)]
        api.GetFileVersionInfoSizeW.restype = wintypes.DWORD
        api.GetFileVersionInfoW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
        api.GetFileVersionInfoW.restype = wintypes.BOOL
        api.VerQueryValueW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(wintypes.UINT)]
        api.VerQueryValueW.restype = wintypes.BOOL
        size = api.GetFileVersionInfoSizeW(str(copied), None)
        self.assertGreater(size, 0)
        buffer = ctypes.create_string_buffer(size)
        self.assertTrue(api.GetFileVersionInfoW(str(copied), 0, size, buffer))
        pointer, length = ctypes.c_void_p(), wintypes.UINT()
        self.assertTrue(api.VerQueryValueW(buffer, '\\StringFileInfo\\040904b0\\ProductName', ctypes.byref(pointer), ctypes.byref(length)))
        self.assertEqual(ctypes.wstring_at(pointer), 'My Rocket Game')
        run = subprocess.run([str(copied), 'capabilities'], cwd=self.root, capture_output=True, timeout=20)
        self.assertEqual(run.returncode, 0, run.stderr.decode('utf-8', 'replace'))
        self.assertIsInstance(json.loads(run.stdout), dict)


if __name__ == '__main__': unittest.main(verbosity=2)
