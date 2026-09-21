"""Host-side contracts, with no SDK install, network use or attached device required."""
import argparse
import importlib.util
import json
from pathlib import Path
import tempfile
import struct
import unittest
import subprocess
from unittest.mock import patch
import zipfile

spec = importlib.util.spec_from_file_location('langmake_android', Path(__file__).with_name('android.py'))
android = importlib.util.module_from_spec(spec)
spec.loader.exec_module(android)


class AndroidToolTests(unittest.TestCase):
    def setUp(self):
        folder = android.ROOT / '.cache/android/tests'
        folder.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=folder)
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def test_application_metadata_cli_defaults_and_package_verification(self):
        parsed = android.parser().parse_args(['build', '--project', 'game', '--profile', 'phone', '--version', '2.1', '--build-number', '9'])
        self.assertIsNone(parsed.application_id); self.assertIsNone(parsed.label)
        self.assertEqual((parsed.profile, parsed.version, parsed.build_number), ('phone', '2.1', 9))
        app = {'id': 'org.example.game', 'version': '2.1', 'build': 9}
        apk = self.root / 'game.apk'
        with zipfile.ZipFile(apk, 'w') as archive:
            archive.writestr('res/mipmap-anydpi-v26/ic_launcher.xml', b'compiled adaptive icon')
            archive.writestr('res/mipmap-xxxhdpi-v4/ic_launcher_background.png', b'packaged icon')
        badging = b"package: name='org.example.game' versionCode='9' versionName='2.1'\napplication-icon-640:'res/mipmap-anydpi-v26/ic_launcher.xml'\n"
        with patch.object(android, 'run', return_value=subprocess.CompletedProcess([], 0, badging, b'')):
            report = android.verify_package_metadata(apk, app, True)
            self.assertEqual(report['build'], 9); self.assertEqual(len(report['icons']), 2)
            with self.assertRaisesRegex(ValueError, 'identity/version'):
                android.verify_package_metadata(apk, dict(app, build=10), True)
        with patch.object(android, 'run', return_value=subprocess.CompletedProcess([], 0, badging.splitlines()[0], b'')), self.assertRaisesRegex(ValueError, 'no packaged'):
            android.verify_package_metadata(apk, app, True)

    def test_native_cache_keys_abi_configuration_and_toolchain(self):
        shader = self.root / 'shaderc'; shader.write_bytes(b'fixture')
        first = android.native_cache_key('arm64-v8a', 'debug', shader)
        self.assertNotEqual(first, android.native_cache_key('x86_64', 'debug', shader))
        self.assertNotEqual(first, android.native_cache_key('arm64-v8a', 'release', shader))
        with patch.object(android, 'LOCK', dict(android.LOCK, ndk='different')):
            self.assertNotEqual(first, android.native_cache_key('arm64-v8a', 'debug', shader))

    def test_workspace_containment(self):
        self.assertEqual(android.scoped(self.root), self.root)
        with self.assertRaises(ValueError): android.scoped(android.ROOT.parent / 'outside')

    def test_device_hardware_class_uses_explicit_property_not_serial(self):
        listing = b'List of devices attached\nemulator-looking device\nphone-looking device\nunknown device\nlocked unauthorized\noffline offline\n'
        def observed(args, command, **options):
            if command == ['devices', '-l']:
                return subprocess.CompletedProcess([], 0, listing, b'')
            self.assertEqual(command, ['shell', 'getprop', 'ro.kernel.qemu'])
            self.assertEqual(options['timeout'], 5)
            markers = {'emulator-looking': b'0\n', 'phone-looking': b'1\n', 'unknown': b''}
            return subprocess.CompletedProcess([], 0, markers[args.serial], b'')
        with patch.object(android, 'adb', side_effect=observed) as call:
            result = android.devices(argparse.Namespace(serial=None))
        self.assertEqual([v['is_emulator'] for v in result['devices']], [False, True, None, None, None])
        self.assertIn('unknown', result['devices'][2]['classification_diagnostic'])
        self.assertEqual(call.call_count, 4)
        with patch.object(android, 'adb', side_effect=[subprocess.CompletedProcess([], 0, b'List of devices attached\ntimeout device\n', b''), subprocess.TimeoutExpired('adb', 5)]):
            item = android.devices(argparse.Namespace(serial=None))['devices'][0]
            self.assertIsNone(item['is_emulator'])
            self.assertIn('classification_diagnostic', item)

    def test_explicit_game_selection_can_be_sibling_of_engine(self):
        engine = self.root / 'engine'; engine.mkdir()
        game = self.root / 'game'; game.mkdir()
        with patch.object(android, 'ROOT', engine):
            self.assertEqual(android.selected_path(game), game)
            with self.assertRaises(ValueError): android.scoped(game)

    def test_java_batch_launchers_never_use_cmd_shell(self):
        with patch.object(android.subprocess, 'run') as process:
            process.return_value.returncode = 0
            android.run([android.GRADLE / 'bin/gradle.bat', '-PlangmakeLabel=Rockets&Aliens'])
            command = process.call_args[0][0]
            self.assertEqual(Path(command[0]), android.JDK / 'bin/java.exe')
            self.assertEqual(command[-1], '-PlangmakeLabel=Rockets&Aliens')
            self.assertFalse(process.call_args[1].get('shell', False))

    def test_archive_traversal_and_symlink_rejected(self):
        for name in ['../outside', '/absolute', 'C:/outside', 'folder\\outside']:
            archive = self.root / 'bad.zip'
            with zipfile.ZipFile(archive, 'w') as z:
                entry = zipfile.ZipInfo(); entry.filename = name
                z.writestr(entry, b'bad')
            with self.assertRaises(ValueError): android.extract(archive, self.root / 'extract')
        archive = self.root / 'link.zip'
        with zipfile.ZipFile(archive, 'w') as z:
            info = zipfile.ZipInfo('link'); info.external_attr = 0o120777 << 16
            z.writestr(info, 'target')
        with self.assertRaises(ValueError): android.extract(archive, self.root / 'extract')

    def test_archive_extracts_only_expected_root(self):
        archive = self.root / 'good.zip'
        with zipfile.ZipFile(archive, 'w') as z: z.writestr('expected/bin/tool', b'data')
        android.extract(archive, self.root / 'output', 'expected')
        self.assertEqual((self.root / 'output/bin/tool').read_bytes(), b'data')
        with self.assertRaises(ValueError): android.extract(archive, self.root / 'other', 'wrong')

    def test_bootstrap_does_not_accept_license_implicitly(self):
        with patch.object(android, 'install_archive') as install:
            result = android.bootstrap(argparse.Namespace(accept_sdk_licenses=False, emulator=False))
        self.assertEqual(result['code'], 'sdk_license_acceptance_required')
        self.assertEqual(install.call_count, 2)  # Only JDK + Gradle.

    def test_device_mutations_require_serial(self):
        with patch.object(android, 'adb_executable', return_value='adb'), patch.object(android, 'run') as run:
            for serial in ['', 'phone;rm', 'phone\nother']:
                with self.assertRaises(ValueError): android.adb(argparse.Namespace(serial=serial), ['install', 'test.apk'])
            run.assert_not_called()

    def private_args(self, command='pull'):
        arguments=[command,'--serial','fixture-phone','--application-id','org.test.game','--storage-id','a'*24]
        if command=='pull':arguments+=['--path','saves/progress.json','--output',str(self.root/'received.bin')]
        return android.parser().parse_args(arguments)

    def test_private_pull_preserves_binary_with_status_aware_nonpty_shell(self):
        payload=b'\x00\xff\x89PNG\r\n\x1a\ncat: this is legitimate file content\x00\r\n'
        with patch.object(android,'adb_executable',return_value='adb'),patch.object(android,'env',return_value={}),patch.object(android.subprocess,'run',return_value=subprocess.CompletedProcess([],0,payload,b'')) as call:
            result=android.device_command(self.private_args())
        self.assertTrue(result['ok']);self.assertEqual((self.root/'received.bin').read_bytes(),payload)
        self.assertEqual(call.call_args.args[0],['adb','-P','5038','-s','fixture-phone','shell','-T','run-as','org.test.game','cat','files/state/'+'a'*24+'/saves/progress.json'])
        self.assertEqual(call.call_args.kwargs['timeout'],30)
        self.assertEqual(result['sha256'],android.digest(self.root/'received.bin'))
        self.assertEqual(list(self.root.glob('*.part')),[])

    def test_private_missing_file_never_becomes_a_successful_output(self):
        args=self.private_args()
        failure=subprocess.CompletedProcess([],1,b'',b'cat: files/state/id/saves/progress.json: No such file or directory')
        with patch.object(android,'adb_executable',return_value='adb'),patch.object(android,'env',return_value={}),patch.object(android.subprocess,'run',return_value=failure):
            with self.assertRaisesRegex(RuntimeError,'No such file'):android.device_command(args)
        self.assertFalse(Path(args.output).exists())

    def test_private_partial_read_failure_preserves_existing_evidence(self):
        args=self.private_args();Path(args.output).write_bytes(b'existing valid evidence')
        failure=subprocess.CompletedProcess([],1,b'partial bytes',b'cat: input/output error')
        with patch.object(android,'adb_executable',return_value='adb'),patch.object(android,'env',return_value={}),patch.object(android.subprocess,'run',return_value=failure):
            with self.assertRaisesRegex(RuntimeError,'input/output error'):android.device_command(args)
        self.assertEqual(Path(args.output).read_bytes(),b'existing valid evidence')

    def test_private_atomic_publication_failure_does_not_truncate_old_output(self):
        args=self.private_args();Path(args.output).write_bytes(b'previous')
        with patch.object(android,'adb',return_value=subprocess.CompletedProcess([],0,b'new',b'')),patch.object(Path,'replace',side_effect=OSError('publication blocked')):
            with self.assertRaisesRegex(OSError,'publication blocked'):android.device_command(args)
        self.assertEqual(Path(args.output).read_bytes(),b'previous');self.assertEqual(list(self.root.glob('*.part')),[])

    def test_private_inspection_checks_remote_status_before_json(self):
        args=self.private_args('inspect')
        with patch.object(android,'adb_executable',return_value='adb'),patch.object(android,'env',return_value={}),patch.object(android.subprocess,'run',return_value=subprocess.CompletedProcess([],1,b'',b'cat: live.json: No such file or directory')):
            with self.assertRaisesRegex(RuntimeError,'No such file'):android.device_command(args)
        with patch.object(android,'adb',return_value=subprocess.CompletedProcess([],0,b'{"session":"observed","running":true}',b'')) as adb:
            result=android.device_command(args)
        self.assertEqual(result['live']['session'],'observed');self.assertEqual(adb.call_args.args[1][:2],['shell','-T'])

    def test_portable_doctor_needs_no_native_toolchain(self):
        (self.root / 'android-player').mkdir()
        sdk = self.root / 'sdk'; jdk = self.root / 'jdk'; gradle = self.root / 'gradle'
        for path in [jdk / 'bin/java.exe', gradle / 'bin/gradle.bat', sdk / 'platforms/android-36/android.jar',
                     sdk / 'build-tools' / android.LOCK['build_tools'] / 'aapt2.exe', sdk / 'platform-tools/adb.exe']:
            path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(b'fixture')
        args = argparse.Namespace(player_only=False)
        with patch.object(android, 'ROOT', self.root), patch.object(android, 'SDK', sdk), \
             patch.object(android, 'JDK', jdk), patch.object(android, 'GRADLE', gradle), \
             patch.object(android.shutil, 'which', return_value=None):
            portable = android.doctor(args)
            self.assertTrue(portable['ok'])
            self.assertEqual(portable['mode'], 'prebuilt-player')
            self.assertNotIn('ndk', [check['name'] for check in portable['checks']])
            (self.root / 'CMakeLists.txt').write_text('# source checkout')
            self.assertFalse(android.doctor(args)['ok'])
            self.assertEqual(android.doctor(args)['mode'], 'source-build')

    def test_portable_bootstrap_skips_ndk_without_implicit_license_acceptance(self):
        (self.root / 'android-player').mkdir()
        tools = self.root / 'tools'; tools.mkdir()
        args = argparse.Namespace(player_only=False, accept_sdk_licenses=False, emulator=False)
        with patch.object(android, 'ROOT', self.root), patch.object(android, 'TOOLS', tools), \
             patch.object(android, 'SDK', tools / 'sdk'), patch.object(android, 'install_archive') as install, \
             patch.object(android, 'write_sdk_metadata'), patch.object(android, 'doctor', return_value={'ok':True}):
            self.assertEqual(android.bootstrap(args)['code'], 'sdk_license_acceptance_required')
            self.assertEqual(install.call_count, 2)
            self.assertFalse((tools / 'sdk-license-acceptance.json').exists())
            install.reset_mock()
            args.accept_sdk_licenses = True
            self.assertTrue(android.bootstrap(args)['ok'])
            installed = [call.args[0].get('id', '') for call in install.call_args_list]
            self.assertFalse(any(name.startswith('ndk;') for name in installed))
            self.assertIn('platform-tools', installed)

    def test_orientation_defaults_and_explicit_modes(self):
        command = ['build', '--project', 'game', '--application-id', 'org.langmake.fixture']
        self.assertIsNone(android.parser().parse_args(command).orientation)
        self.assertEqual(android.mobile_assets.metadata({'title': 'Legacy'}, 'android', overrides={'id': 'org.example.game'})['orientation'], 'landscape')
        for mode in ['landscape', 'portrait', 'sensor']:
            self.assertEqual(android.parser().parse_args(command + ['--orientation', mode]).orientation, mode)
        with patch('sys.stderr'), self.assertRaises(SystemExit):
            android.parser().parse_args(command + ['--orientation', 'free-form'])

    def test_portable_build_discovers_prebuilt_player_without_source_tree(self):
        (self.root / 'android-player/arm64-v8a').mkdir(parents=True)
        (self.root / 'langmake.exe').write_bytes(b'host')
        args = argparse.Namespace(player_dir=None, host_build=None)
        with patch.object(android, 'ROOT', self.root):
            player, portable, host = android.build_inputs(args)
        self.assertTrue(portable)
        self.assertEqual(player, self.root / 'android-player')
        self.assertEqual(host, self.root)

    def test_host_discovery_priority_and_explicit_override(self):
        (self.root / 'CMakeLists.txt').write_text('# source project')
        current = self.root / 'build'; current.mkdir(); (current / 'langmake.exe').write_bytes(b'current')
        legacy = self.root / '.cache/build-0.4'; legacy.mkdir(parents=True); (legacy / 'langmake.exe').write_bytes(b'0.4')
        args = argparse.Namespace(player_dir=None, host_build=None)
        with patch.object(android, 'ROOT', self.root):
            self.assertEqual(android.build_inputs(args)[2], current)
            newest = self.root / '.cache/build-0.4.1'; newest.mkdir(parents=True); (newest / 'langmake.exe').write_bytes(b'0.4.1')
            self.assertEqual(android.build_inputs(args)[2], current)
            (self.root / 'langmake.exe').write_bytes(b'packaged')
            self.assertEqual(android.build_inputs(args)[2], self.root)
            args.host_build = 'build'
            self.assertEqual(android.build_inputs(args)[2], current)

    def test_old_host_fails_before_sdk_or_build_work(self):
        (self.root / 'CMakeLists.txt').write_text('# source project')
        (self.root / 'build').mkdir(); (self.root / 'build/langmake.exe').write_bytes(b'fixture')
        args = argparse.Namespace(player_dir=None, host_build=None, skip_native=False, configuration='debug', abi='arm64-v8a')
        response = argparse.Namespace(returncode=0, stdout=b'{"version":"0.3.0","commands":["validate"]}')
        with patch.object(android, 'ROOT', self.root), patch.object(android, 'run', return_value=response), patch.object(android, 'doctor') as doctor:
            with self.assertRaisesRegex(ValueError, 'does not support cook'):
                android.build(args)
            doctor.assert_not_called()

    def test_current_host_cook_capability(self):
        (self.root / 'langmake.exe').write_bytes(b'fixture')
        response = argparse.Namespace(returncode=0, stdout=b'{"version":"0.4.0","commands":["cook"]}')
        with patch.object(android, 'run', return_value=response):
            self.assertEqual(android.host_capabilities(self.root)['version'], '0.4.0')

    def test_package_without_android_player_fails_before_toolchain_check(self):
        args = argparse.Namespace(player_dir=None, host_build=None, skip_native=False, configuration='debug', abi='arm64-v8a')
        with patch.object(android, 'ROOT', self.root), patch.object(android, 'doctor') as doctor:
            with self.assertRaisesRegex(ValueError, 'does not include an Android player'):
                android.build(args)
            doctor.assert_not_called()

    def test_incomplete_player_fails_before_toolchain_check(self):
        (self.root / 'android-player/arm64-v8a').mkdir(parents=True)
        (self.root / 'android-player/arm64-v8a/libmain.so').write_bytes(b'placeholder')
        args = argparse.Namespace(player_dir=None, host_build=None, skip_native=False, configuration='debug', abi='arm64-v8a')
        with patch.object(android, 'ROOT', self.root), patch.object(android, 'doctor') as doctor:
            with self.assertRaisesRegex(ValueError, 'package is incomplete'):
                android.build(args)
            doctor.assert_not_called()

    def test_source_release_cannot_skip_native_build(self):
        (self.root / 'CMakeLists.txt').write_text('# source project')
        args = argparse.Namespace(player_dir=None, host_build=None, skip_native=True, configuration='release', abi='arm64-v8a')
        with patch.object(android, 'ROOT', self.root), patch.object(android, 'doctor') as doctor:
            with self.assertRaisesRegex(ValueError, 'skip-native cannot'):
                android.build(args)
            doctor.assert_not_called()

    def test_native_cache_abi_and_development_configuration(self):
        cache = self.root / 'CMakeCache.txt'
        cache.write_text('ANDROID_ABI:STRING=arm64-v8a\nLM_ANDROID_DEVELOPMENT:BOOL=ON\n')
        self.assertTrue(android.native_cache_configuration(self.root, 'arm64-v8a', 'debug'))
        with self.assertRaises(ValueError): android.native_cache_configuration(self.root, 'arm64-v8a', 'release')
        with self.assertRaises(ValueError): android.native_cache_configuration(self.root, 'x86_64', 'debug')
        cache.write_text('ANDROID_ABI:STRING=arm64-v8a\n')
        with self.assertRaises(ValueError): android.native_cache_configuration(self.root, 'arm64-v8a', 'release')

    def test_prebuilt_release_requires_matching_library_metadata(self):
        library = self.root / 'arm64-v8a/libmain.so'; library.parent.mkdir(); library.write_bytes(b'fixture')
        metadata = library.with_name('player.json')
        with self.assertRaises(ValueError): android.prebuilt_configuration(self.root, 'arm64-v8a', 'release', library)
        metadata.write_text(json.dumps({'development':False, 'abi':'arm64-v8a', 'sha256':android.digest(library)}))
        self.assertFalse(android.prebuilt_configuration(self.root, 'arm64-v8a', 'release', library))
        library.write_bytes(b'changed')
        with self.assertRaises(ValueError): android.prebuilt_configuration(self.root, 'arm64-v8a', 'release', library)

    def test_prebuilt_variant_selection_preserves_legacy_and_separates_configs(self):
        abi = self.root / 'arm64-v8a'; abi.mkdir()
        legacy = abi / 'libmain.so'; legacy.write_bytes(b'legacy')
        self.assertEqual(android.prebuilt_library(self.root, 'arm64-v8a', 'debug'), legacy)
        for config in ['debug', 'release']:
            variant = abi / config; variant.mkdir()
            library = variant / 'libmain.so'; library.write_bytes(config.encode())
            (variant / 'player.json').write_text(json.dumps({'development':config=='debug', 'abi':'arm64-v8a', 'sha256':android.digest(library)}))
            self.assertEqual(android.prebuilt_library(self.root, 'arm64-v8a', config), library)
            self.assertEqual(android.prebuilt_configuration(self.root, 'arm64-v8a', config, library), config=='debug')
        (abi / 'release/libmain.so').unlink()
        self.assertEqual(android.prebuilt_library(self.root, 'arm64-v8a', 'release'), abi / 'release/libmain.so')

    def test_native_abi_and_16k_elf_segments_are_checked(self):
        data = bytearray(120)
        data[:6] = b'\x7fELF\x02\x01'
        struct.pack_into('<H', data, 18, 183)
        struct.pack_into('<Q', data, 32, 64)
        struct.pack_into('<HH', data, 54, 56, 1)
        struct.pack_into('<IIQQQQQQ', data, 64, 1, 5, 0, 0, 0, 120, 120, 16384)
        file = self.root / 'libmain.so'; file.write_bytes(data)
        self.assertEqual(android.verify_native_library(file, 'arm64-v8a')['page_alignment'], 16384)
        with self.assertRaises(ValueError): android.verify_native_library(file, 'x86_64')
        struct.pack_into('<Q', data, 112, 4096); file.write_bytes(data)
        with self.assertRaises(ValueError): android.verify_native_library(file, 'arm64-v8a')

    def test_shell_paths_are_bounded(self):
        sid = 'a' * 24
        self.assertEqual(android.bridge_path(sid, 'commands/test.json'), 'files/state/' + sid + '/commands/test.json')
        for value in ['../outside', '/absolute', "x';whoami", 'x y', 'x\\y']:
            with self.assertRaises(ValueError): android.bridge_path(sid, value)
        with self.assertRaises(ValueError): android.valid_app('a.b;whoami')

    def test_packaged_native_evidence_uses_apk_bytes(self):
        data = bytearray(120)
        data[:6] = b'\x7fELF\x02\x01'
        struct.pack_into('<H', data, 18, 62)
        struct.pack_into('<Q', data, 32, 64)
        struct.pack_into('<HH', data, 54, 56, 1)
        struct.pack_into('<IIQQQQQQ', data, 64, 1, 5, 0, 0, 0, 120, 120, 16384)
        apk = self.root / 'test.apk'
        with zipfile.ZipFile(apk, 'w') as archive:
            archive.writestr('lib/x86_64/libmain.so', data)
        output = self.root / 'extracted/libmain.so'
        result = android.verify_packaged_library(apk, 'x86_64', output)
        self.assertEqual(output.read_bytes(), data)
        self.assertEqual(result['sha256'], android.digest(output))
        self.assertNotEqual(result['sha256'], android.digest(apk))
        with self.assertRaises(ValueError):
            android.verify_packaged_library(apk, 'arm64-v8a', output)
        struct.pack_into('<Q', data, 112, 4096)
        with zipfile.ZipFile(apk, 'w') as archive:
            archive.writestr('lib/x86_64/libmain.so', data)
        with self.assertRaises(ValueError):
            android.verify_packaged_library(apk, 'x86_64', output)

    def test_bundle_uses_registered_graph_and_separates_save_identity(self):
        cooked = self.root / 'cooked'; runtime = self.root / 'runtime'
        cooked.mkdir(); (runtime / 'shaders').mkdir(parents=True); (runtime / 'fonts').mkdir()
        (cooked / 'project.json').write_text('{"project_id":"stable"}')
        (cooked / 'game.luau').write_text('return {}')
        (cooked / 'private.txt').write_text('must not ship')
        (runtime / 'shaders/vs_cubes.bin').write_bytes(b'shader')
        (runtime / 'fonts/LatoLatin-Regular.ttf').write_bytes(b'font')
        (cooked / 'cook-manifest.json').write_text(json.dumps({'format_version':1, 'target':'android',
            'project_manifest':'project.json', 'source_revision':'one', 'packaged_revision':'renamed-manifest',
            'files':[{'path':'project.json'}, {'path':'game.luau'}]}))
        assets = self.root / 'assets'; assets.mkdir()
        first = android.stage_bundle(cooked, runtime, assets)
        self.assertFalse((assets / 'game/private.txt').exists())
        self.assertTrue(all(len(entry['sha256']) == 64 for entry in first['files']))
        self.assertEqual(first['source_revision'], 'one')
        self.assertEqual(first['packaged_revision'], 'renamed-manifest')
        (cooked / 'game.luau').write_text('return {version=2}')
        second = android.stage_bundle(cooked, runtime, assets)
        self.assertEqual(first['storage_id'], second['storage_id'])
        self.assertNotEqual(first['content_id'], second['content_id'])


if __name__ == '__main__': unittest.main()
