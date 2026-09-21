"""Host-side iOS contracts. These tests do not claim an Apple build or device run."""
import argparse
import importlib.util
import io
import json
import subprocess
import sys
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('langmake_ios', Path(__file__).with_name('ios.py'))
ios = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ios)


class IosToolTests(unittest.TestCase):
    def setUp(self):
        folder = ios.ROOT / '.cache/ios/tests'
        folder.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=folder)
        self.root = Path(self.temporary.name)
        self.cooked = self.root / 'cooked'
        self.cooked.mkdir()
        self.resources = []
        for name in ['shaders/vs_cubes.bin', 'shaders/fs_cubes.bin', 'fonts/LatoLatin-Regular.ttf']:
            path = self.root / 'resources' / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'fixture-resource-' + name.encode())
            self.resources.append((name, path))
        self.write_project({'title': 'Fixture', 'project_id': 'stable-game'})

    def tearDown(self):
        self.temporary.cleanup()

    def write_project(self, project):
        ios.write_json(self.cooked / 'project.json', project)
        (self.cooked / 'main.luau').write_text('return {}\n', encoding='utf-8')
        files = [{'path': name, 'bytes': (self.cooked / name).stat().st_size,
                  'revision': ios.source_revision(self.cooked / name)} for name in ['project.json', 'main.luau']]
        ios.write_json(self.cooked / 'cook-manifest.json', {'format_version': 1, 'target': 'ios', 'project_manifest': 'project.json',
                       'source_revision': 'fnv1a64-source', 'packaged_revision': 'fnv1a64-package', 'files': files})

    def stage(self, name='staged', configuration='debug'):
        output = self.root / name
        report = ios.stage_cooked(self.cooked, output, 'org.example.fixture', 'Rockets & Aliens',
                                  configuration=configuration, resources=self.resources)
        return output, report

    def test_stage_complete_inventory_and_revision_identity(self):
        folder, report = self.stage()
        manifest = ios.verify_stage(folder)
        self.assertEqual(report['packaged_revision'], 'fnv1a64-package')
        self.assertEqual(manifest['source_revision'], 'fnv1a64-source')
        self.assertEqual(manifest['project_manifest'], 'project/project.json')
        self.assertEqual(len(manifest['files']), 6)
        self.assertFalse(report['device_qualified'])
        self.assertTrue(manifest['development'])
        self.assertEqual(manifest['content_bytes'], sum(x['bytes'] for x in manifest['files']))

    def test_cli_unicode_json_roundtrip_under_windows_console_encoding(self):
        self.write_project({'title': 'Aventure — 宇宙 🚀', 'project_id': 'stable-game'})
        folder = self.root / 'unicode-stage'
        ios.stage_cooked(self.cooked, folder, 'org.example.fixture', 'Aventure — 宇宙 🚀', resources=self.resources)
        command = [sys.executable, str(Path(ios.__file__)), 'verify-stage', '--staged', str(folder)]
        process = subprocess.run(command, cwd=ios.ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(process.returncode, 0, process.stderr.decode('utf-8', 'replace'))
        payload = json.loads(process.stdout.decode('utf-8'))
        self.assertTrue(payload['ok'])
        self.assertEqual(payload['manifest']['project_id'], 'stable-game')
        self.assertEqual(payload['manifest']['label'], 'Aventure — 宇宙 🚀')
        self.assertEqual((folder / 'project/project.json').read_text(encoding='utf-8').count('宇宙'), 1)

    def test_title_change_keeps_declared_storage_identity(self):
        _, first = self.stage('one')
        self.write_project({'title': 'Renamed Game', 'project_id': 'stable-game'})
        _, second = self.stage('two')
        self.assertEqual(first['storage_id'], second['storage_id'])
        self.assertNotEqual(first['content_id'], second['content_id'])

    def test_legacy_title_is_storage_identity(self):
        self.write_project({'title': 'Legacy'})
        _, one = self.stage('one')
        self.write_project({'title': 'Changed Legacy'})
        _, two = self.stage('two')
        self.assertNotEqual(one['storage_id'], two['storage_id'])

    def test_existing_stage_not_overwritten(self):
        folder, _ = self.stage()
        original = (folder / 'manifest.json').read_bytes()
        with self.assertRaises(ValueError): self.stage()
        self.assertEqual(original, (folder / 'manifest.json').read_bytes())

    def test_workspace_paths_do_not_escape(self):
        self.assertEqual(ios.scoped(self.root), self.root)
        with self.assertRaises(ValueError): ios.scoped(ios.ROOT.parent / 'outside')
        for name in ['../outside', '/root', 'a/../../b', 'a\\b', 'C:/file', 'a//b', './a', 'a/./b', 'a\0b', 'a\nb']:
            with self.subTest(name=name), self.assertRaises(ValueError): ios.relative_path(name)

    def test_cook_integrity_rejects_same_length_edits(self):
        path = self.cooked / 'main.luau'
        path.write_text('return []\n', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'changed'): self.stage()

    def test_rejects_case_collisions(self):
        manifest = ios.read_json(self.cooked / 'cook-manifest.json')
        manifest['files'].append({'path': 'MAIN.LUAU', 'bytes': 10, 'revision': ''})
        ios.write_json(self.cooked / 'cook-manifest.json', manifest)
        with self.assertRaisesRegex(ValueError, 'case-colliding'): self.stage()

    def test_rejects_non_ios_cook(self):
        manifest = ios.read_json(self.cooked / 'cook-manifest.json'); manifest['target'] = 'android'
        ios.write_json(self.cooked / 'cook-manifest.json', manifest)
        with self.assertRaisesRegex(ValueError, 'iOS cook'): self.stage()

    def test_oversize_stage_rejected_before_copy(self):
        with patch.object(ios, 'LIMIT', 1), self.assertRaisesRegex(ValueError, 'budget'): self.stage()
        self.assertFalse((self.root / 'staged').exists())

    def test_staged_tampering_detected(self):
        folder, _ = self.stage()
        (folder / 'project/main.luau').write_bytes(b'return []\n')
        with self.assertRaisesRegex(ValueError, 'integrity'): ios.verify_stage(folder)

    def test_unregistered_staged_files_rejected(self):
        folder, _ = self.stage()
        (folder / 'unregistered.txt').write_text('no', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'Unregistered'): ios.verify_stage(folder)

    def test_manifest_identity_tampering_detected(self):
        folder, _ = self.stage()
        manifest = ios.read_json(folder / 'manifest.json'); manifest['storage_id'] = 'a' * 24
        ios.write_json(folder / 'manifest.json', manifest)
        with self.assertRaisesRegex(ValueError, 'checksum'): ios.verify_stage(folder)

    def test_release_metadata_consistent(self):
        folder, _ = self.stage(configuration='release')
        self.assertFalse(ios.verify_stage(folder)['development'])
        manifest = ios.read_json(folder / 'manifest.json'); manifest['development'] = True
        ios.write_json(folder / 'manifest.json', manifest)
        with self.assertRaisesRegex(ValueError, 'disagree'): ios.verify_stage(folder)

    def test_symlink_cooked_source_rejected(self):
        path = self.cooked / 'main.luau'
        path.unlink()
        try:
            path.symlink_to(self.resources[0][1])
        except OSError:
            self.skipTest('Host does not allow creating test symlinks.')
        with self.assertRaisesRegex(ValueError, 'Symlinks'): self.stage()

    def build_args(self, **overrides):
        values = dict(destination='device', arch='arm64', team_id='AB12345678', signing_identity=None,
                      provisioning_profile=None, allow_provisioning_updates=False, staged=str(self.root / 'staged'))
        values.update(overrides)
        return argparse.Namespace(**values)

    def test_build_flags_keep_device_configuration_and_label_literal(self):
        folder, _ = self.stage()
        command = ios.build_command(self.build_args(), ios.verify_stage(folder), self.root / 'build', self.root / 'shaderc')
        self.assertIn('-DCMAKE_SYSTEM_NAME=iOS', command)
        self.assertIn('-DCMAKE_OSX_SYSROOT=iphoneos', command)
        self.assertIn('-DLM_IOS_LABEL=Rockets & Aliens', command)
        self.assertIn('-DLM_IOS_DEVELOPMENT=ON', command)
        self.assertIn('-DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=AB12345678', command)

    def test_simulator_unsigned_release(self):
        folder, _ = self.stage(configuration='release')
        command = ios.build_command(self.build_args(destination='simulator', team_id=None, arch='x86_64'),
                                    ios.verify_stage(folder), self.root / 'build', self.root / 'shaderc')
        self.assertIn('-DCMAKE_OSX_SYSROOT=iphonesimulator', command)
        self.assertIn('-DLM_IOS_DEVELOPMENT=OFF', command)
        self.assertIn('-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO', command)

    def test_device_signing_and_arch_required(self):
        folder, _ = self.stage(); manifest = ios.verify_stage(folder)
        for values in [dict(team_id=None), dict(team_id='not-a-team'), dict(arch='x86_64'), dict(destination='simulator'),
                       dict(destination='simulator', team_id=None, signing_identity='Apple Development')]:
            with self.subTest(values=values), self.assertRaises(ValueError):
                ios.build_command(self.build_args(**values), manifest, self.root / 'build', self.root / 'shaderc')

    def test_selected_device_build_uses_scheme_hardware_udid_and_local_derived_data(self):
        folder, _ = self.stage(); manifest = ios.verify_stage(folder)
        args = self.build_args(device='12345678-1234-1234-1234-123456789012', register_device=True,
                               allow_provisioning_updates=True, jobs=8)
        work = self.root / 'build'
        configured = ios.build_command(args, manifest, work, self.root / 'shaderc')
        self.assertIn('-DCMAKE_XCODE_GENERATE_SCHEME=ON', configured)
        record = ios.device_record(self.physical_record(args.device, modern=True))
        command = ios.native_build_command(args, manifest, work, record)
        self.assertEqual(command[0], 'xcodebuild')
        self.assertEqual(command[command.index('-scheme') + 1], 'langmake')
        self.assertNotIn('-target', command)
        self.assertEqual(command[command.index('-destination') + 1], 'id=00000000-0000000000000002')
        self.assertNotIn('id=' + args.device, command)
        self.assertEqual(Path(command[command.index('-derivedDataPath') + 1]), work / 'DerivedData')
        self.assertIn('-allowProvisioningUpdates', command)
        self.assertIn('-allowProvisioningDeviceRegistration', command)
        self.assertEqual(command[-1], 'build')
        args.register_device = False
        self.assertNotIn('-allowProvisioningDeviceRegistration', ios.native_build_command(args, manifest, work, record))
        args.allow_provisioning_updates = False
        self.assertNotIn('-allowProvisioningUpdates', ios.native_build_command(args, manifest, work, record))
        ordinary = ios.native_build_command(self.build_args(jobs=4), manifest, work)
        self.assertEqual(ordinary, ['cmake', '--build', work, '--config', 'Debug', '--target', 'langmake', '--parallel', '4'])

    def test_device_registration_requires_explicit_device_and_provisioning_permission(self):
        folder, _ = self.stage(); manifest = ios.verify_stage(folder)
        cases = [dict(register_device=True), dict(register_device=True, allow_provisioning_updates=True),
                 dict(device='12345678', register_device=True), dict(device='booted'),
                 dict(destination='simulator', team_id=None, device='12345678'),
                 dict(destination='simulator', team_id=None, register_device=True)]
        for values in cases:
            with self.subTest(values=values), self.assertRaises(ValueError):
                ios.build_command(self.build_args(**values), manifest, self.root / 'build', self.root / 'shaderc')
        parsed = ios.parser().parse_args(['build', '--destination', 'device', '--staged', str(folder), '--team-id', 'AB12345678',
                                         '--device', '12345678', '--allow-provisioning-updates', '--register-device'])
        self.assertEqual(parsed.device, '12345678'); self.assertTrue(parsed.register_device)

    def test_selected_build_verifies_physical_device_and_udid_before_work(self):
        folder, _ = self.stage()
        args = self.build_args(device='12345678', staged=folder, output=self.root / 'output', shaderc=None, jobs=4)
        for failure in [ValueError('simulated target'), {'id': args.device, 'reality': 'physical', 'udid': None}]:
            with patch.object(ios, 'require_mac'), patch.object(ios, 'CACHE', self.root / 'build-cache'), \
                    patch.object(ios, 'require_physical_device') as guard, patch.object(ios, 'doctor') as doctor, \
                    patch.object(ios, 'run') as run:
                if isinstance(failure, Exception): guard.side_effect = failure
                else: guard.return_value = failure
                with self.assertRaisesRegex(ValueError, 'simulated|hardware UDID'): ios.build(args)
                guard.assert_called_once_with(args); doctor.assert_not_called(); run.assert_not_called()
                self.assertFalse((self.root / 'build-cache').exists())
                self.assertFalse(Path(args.output).exists())

    def test_selected_build_records_verified_device_and_registration_request(self):
        folder, _ = self.stage(); manifest = ios.verify_stage(folder)
        shaderc = self.root / 'shaderc'; shaderc.write_bytes(b'host shader fixture')
        args = self.build_args(device='12345678', register_device=True, allow_provisioning_updates=True,
                               staged=folder, output=self.root / 'output', shaderc=str(shaderc), jobs=4)
        record = ios.device_record(self.physical_record(args.device, modern=True))
        commands = []
        def run(command, **kwargs):
            commands.append(command)
            if command[0] == 'xcodebuild':
                work = Path(command[command.index('-project') + 1]).parent
                app = work / 'Debug-iphoneos/LangmakePlayer.app'; app.mkdir(parents=True)
                ios.shutil.copytree(folder, app / 'langmake')
                with (app / 'Info.plist').open('wb') as stream:
                    ios.plistlib.dump({'CFBundleIdentifier': manifest['bundle_identifier'], 'CFBundleExecutable': 'LangmakePlayer',
                                      'CFBundleShortVersionString': manifest['application']['version'], 'CFBundleVersion': str(manifest['application']['build'])}, stream)
                (app / 'LangmakePlayer').write_bytes(b'native executable fixture')
            return subprocess.CompletedProcess(command, 0, b'arm64\n' if command[:2] == ['xcrun', 'lipo'] else b'', b'')
        with patch.object(ios, 'require_mac'), patch.object(ios, 'CACHE', self.root / 'build-cache'), \
                patch.object(ios, 'require_physical_device', return_value=record), \
                patch.object(ios, 'doctor', return_value={'can_build': True}), patch.object(ios.os, 'access', return_value=True), \
                patch.object(ios, 'run', side_effect=run):
            report = ios.build(args)
        self.assertEqual(report['selected_device'], record)
        self.assertTrue(report['provisioning_device_registration_requested'])
        self.assertFalse(report['device_qualified'])
        self.assertEqual(ios.read_json(Path(report['report']))['selected_device']['udid'], record['udid'])
        self.assertEqual(sum(command[0] == 'xcodebuild' for command in commands), 1)

    def test_build_host_shader_compiler_targets_running_mac_not_newer_sdk(self):
        folder, _ = self.stage()
        args = self.build_args(destination='simulator', team_id=None, staged=folder,
                               output=self.root / 'app', shaderc=None, jobs=4)
        with patch.object(ios, 'require_mac'), \
                patch.object(ios, 'doctor', return_value={'can_build': True}), \
                patch.object(ios.platform, 'mac_ver', return_value=('26.6.2', ('', '', ''), 'arm64')), \
                patch.object(ios, 'run', side_effect=RuntimeError('stop before native configure')) as run:
            result = ios.build(args)
            self.assertEqual(result['blocking']['code'], 'host_tools_unavailable')
            self.assertIn('stop before native configure', result['blocking']['message'])
        run.assert_called_once()
        host_command = run.call_args.args[0]
        self.assertIn('-DLM_HOST_TOOLS_ONLY=ON', host_command)
        self.assertIn('-DCMAKE_OSX_DEPLOYMENT_TARGET=26.6.2', host_command)
        for destination, team in [('simulator', None), ('device', 'AB12345678')]:
            target_command = ios.build_command(self.build_args(destination=destination, team_id=team),
                                                ios.verify_stage(folder), self.root / 'build', self.root / 'shaderc')
            self.assertIn('-DCMAKE_OSX_DEPLOYMENT_TARGET=17.0', target_command)
            self.assertNotIn('-DCMAKE_OSX_DEPLOYMENT_TARGET=26.6.2', target_command)

    def test_host_shader_compiler_rejects_missing_or_malformed_mac_version(self):
        for version in ['', '26', '26.6-beta', '26.6;other', '26.6\n', '0.1', '٢٦.٦']:
            with self.subTest(version=version), \
                    patch.object(ios.platform, 'mac_ver', return_value=(version, ('', '', ''), 'arm64')), \
                    self.assertRaisesRegex(ValueError, 'running macOS version'):
                ios.host_tools_command(self.root / 'host')

    def test_windows_doctor_is_honest_and_does_not_run_commands(self):
        with patch.object(ios.sys, 'platform', 'win32'), patch.object(ios, 'run') as run:
            result = ios.doctor()
            self.assertFalse(result['can_build']); self.assertTrue(result['can_stage']); run.assert_not_called()
            with self.assertRaisesRegex(ValueError, 'Mac'): ios.require_mac()

    def mac_doctor(self, first_launch_status=0, first_launch_output=b'', first_launch_error=b'', sdk=b'27.0\n'):
        responses = {
            ('xcodebuild', '-version'): (0, b'Xcode 27.0\n', b''),
            ('xcodebuild', '-checkFirstLaunchStatus'): (first_launch_status, first_launch_output, first_launch_error),
            ('xcrun', '--sdk', 'iphoneos', '--show-sdk-version'): (0, sdk, b''),
            ('xcrun', '--sdk', 'iphonesimulator', '--show-sdk-version'): (0, sdk, b''),
            ('xcrun', '--find', 'devicectl'): (0, b'/fixture/devicectl\n', b''),
            ('xcrun', '--find', 'simctl'): (0, b'/fixture/simctl\n', b''),
        }
        def command_result(command, **kwargs):
            # Unknown or mutating commands fail the test instead of reaching the host.
            status, output, error = responses[tuple(command)]
            self.assertFalse(kwargs['check'])
            self.assertEqual(kwargs['timeout'], 30)
            return subprocess.CompletedProcess(command, status, output, error)
        with patch.object(ios.sys, 'platform', 'darwin'), \
                patch.object(ios.platform, 'platform', return_value='macOS-test'), \
                patch.object(ios.shutil, 'which', side_effect=lambda name: '/fixture/' + name), \
                patch.object(ios, 'run', side_effect=command_result) as run:
            result = ios.doctor()
        self.assertEqual({tuple(call.args[0]) for call in run.call_args_list}, set(responses))
        return result

    def test_mac_doctor_blocks_build_when_first_launch_is_incomplete_without_output(self):
        result = self.mac_doctor(first_launch_status=1)
        self.assertFalse(result['ok'])
        self.assertFalse(result['can_build'])
        self.assertTrue(result['can_stage'])
        check = next(item for item in result['checks'] if item['name'] == 'Xcode first-launch setup')
        self.assertFalse(check['ok'])
        self.assertEqual(check['actual'], '')
        self.assertIn('Open the selected Xcode app', check['error'])
        self.assertIn('does not accept licenses or install Xcode components', check['error'])

    def test_mac_doctor_preserves_first_launch_failure_diagnostics(self):
        result = self.mac_doctor(first_launch_status=69, first_launch_output=b'Additional components required\n',
                                 first_launch_error=b'Xcode setup has not completed\n')
        check = next(item for item in result['checks'] if item['name'] == 'Xcode first-launch setup')
        self.assertFalse(result['can_build'])
        self.assertEqual(check['actual'], 'Additional components required')
        self.assertIn('Xcode setup has not completed', check['error'])
        self.assertIn('rerun doctor', check['error'])

    def test_mac_doctor_accepts_completed_first_launch_without_output(self):
        result = self.mac_doctor()
        self.assertTrue(result['ok'])
        self.assertTrue(result['can_build'])
        check = next(item for item in result['checks'] if item['name'] == 'Xcode first-launch setup')
        self.assertTrue(check['ok'])
        self.assertEqual(check['error'], '')

    def test_mac_doctor_completed_setup_does_not_bypass_sdk_minimum(self):
        result = self.mac_doctor(sdk=b'16.4\n')
        self.assertFalse(result['can_build'])
        self.assertTrue(next(item for item in result['checks'] if item['name'] == 'Xcode first-launch setup')['ok'])
        for name in ['iPhoneOS SDK', 'iPhoneSimulator SDK']:
            self.assertFalse(next(item for item in result['checks'] if item['name'] == name)['ok'])

    def test_run_never_uses_shell(self):
        with patch.object(ios.subprocess, 'run') as child:
            child.return_value.returncode = 0
            ios.run(['cmake', '-DLM_IOS_LABEL=Rockets & $(name)'])
            self.assertEqual(child.call_args[0][0][-1], '-DLM_IOS_LABEL=Rockets & $(name)')
            self.assertFalse(child.call_args[1].get('shell', False))

    def test_explicit_device_required(self):
        for value in ['booted', 'all', '', 'x;cat']:
            with self.assertRaises(ValueError): ios.selected_device(argparse.Namespace(device=value))
        self.assertEqual(ios.selected_device(argparse.Namespace(device='00000000-0000000000000001')), '00000000-0000000000000001')

    def physical_record(self, identifier='12345678-1234-1234-1234-123456789012', modern=False):
        hardware = {'reality': 'physical', 'platform': 'iOS', 'deviceType': 'iPhone', 'udid': '00000000-0000000000000002'}
        if modern:
            return {'identifier': identifier, 'properties': {
                'hardware': hardware, 'connection': {'pairingState': 'paired', 'state': 'connected', 'transportType': 'wired'},
                'state': {'name': 'Physical iPhone', 'bootState': 'booted'},
                'software': {'osVersionNumber': {'stringValue': '27.0'}}}}
        return {'identifier': identifier, 'hardwareProperties': hardware,
                'deviceProperties': {'name': 'Physical iPhone', 'osVersionNumber': '17.5', 'developerModeStatus': 'enabled'},
                'connectionProperties': {'pairingState': 'paired', 'tunnelState': 'connected', 'transportType': 'wired'}}

    def test_physical_device_enumeration_handles_legacy_and_xcode27(self):
        old = self.physical_record('00000000-0000000000000001')
        new = self.physical_record(modern=True)
        # Modern values supersede deprecated fields for display and reachability.
        new['connectionProperties'] = {'pairingState': 'unpaired', 'tunnelState': 'disconnected'}
        new['deviceProperties'] = {'name': 'Old name', 'osVersionNumber': '26.0'}
        simulated = self.physical_record('99999999', modern=True)
        simulated['properties']['hardware']['reality'] = 'simulated'
        simulated['properties']['state']['visibilityClass'] = 'simulators'
        with patch.object(ios, 'require_mac'), patch.object(ios, 'device_json', return_value=({'result': {'devices': [old, new, simulated]}}, None)):
            result = ios.devices(argparse.Namespace(destination='device'))
        self.assertEqual([item['id'] for item in result['devices']], [old['identifier'], new['identifier']])
        self.assertEqual([item['os_version'] for item in result['devices']], ['17.5', '27.0'])
        self.assertTrue(all(item['available'] and item['reachable'] and item['reality'] == 'physical' for item in result['devices']))
        self.assertEqual(result['devices'][1]['name'], 'Physical iPhone')
        self.assertEqual(result['excluded_devices'][0]['exclusion_reason'], 'simulated_device')

    def test_ipad_physical_records_use_same_explicit_device_guard(self):
        for modern in (False, True):
            for platform_name in ('iOS', 'iPadOS'):
                with self.subTest(modern=modern, platform=platform_name):
                    row = self.physical_record(modern=modern)
                    hardware = row['properties']['hardware'] if modern else row['hardwareProperties']
                    hardware.update(platform=platform_name, deviceType='iPad')
                    record = ios.device_record(row)
                    self.assertTrue(record['available'])
                    self.assertEqual(record['platform'], platform_name)
                    self.assertEqual(record['reality'], 'physical')
                    with patch.object(ios, 'physical_device_records', return_value=[record]):
                        selected = ios.require_physical_device(argparse.Namespace(device=row['identifier']))
                    self.assertEqual(selected['udid'], hardware['udid'])
                    hardware['reality'] = 'simulated'
                    with patch.object(ios, 'physical_device_records', return_value=[ios.device_record(row)]):
                        with self.assertRaisesRegex(ValueError, 'simulated'):
                            ios.require_physical_device(argparse.Namespace(device=row['identifier']))

    def test_device_pairing_is_not_reachability(self):
        for modern in [False, True]:
            row = self.physical_record(modern=modern)
            connection = row['properties']['connection'] if modern else row['connectionProperties']
            field = 'state' if modern else 'tunnelState'
            connection[field] = 'disconnected'
            record = ios.device_record(row)
            self.assertEqual(record['pairing_state'], 'paired')
            self.assertIs(record['reachable'], False)
            self.assertFalse(record['available'])
            del connection[field]
            record = ios.device_record(row)
            self.assertIsNone(record['reachable'])
            self.assertFalse(record['available'])

    def test_developer_mode_normalizes_xcode27_marker_objects(self):
        for modern, legacy, expected in [
                ({'enabled': {'mode': 1}}, 'enabled', 'enabled'),
                ({'disabled': {}}, 'enabled', 'disabled'),
                ({'enabled': {'mode': 999}}, 'disabled', 'enabled'),
                ('disabled', 'enabled', 'disabled'),
                ({'mode': 1}, 'enabled', 'enabled'),
                ({'unexpected': {}}, 'disabled', 'disabled'),
                ({'mode': 1}, None, None),
                ({'enabled': {}, 'disabled': {}}, None, None),
                (None, 'unknown', None)]:
            with self.subTest(modern=modern, legacy=legacy):
                row = self.physical_record(modern=True)
                row['properties']['state']['developerModeStatus'] = modern
                row['deviceProperties'] = {'developerModeStatus': legacy}
                self.assertEqual(ios.device_record(row)['developer_mode'], expected)

    def test_each_simulator_identity_signal_overrides_paired_physical_label(self):
        for signal in ['legacy_reality', 'modern_reality', 'provider', 'modern_provider', 'visibility', 'modern_visibility']:
            row = self.physical_record(modern=True)
            if signal == 'legacy_reality': row['hardwareProperties'] = {'reality': 'simulated'}
            if signal == 'modern_reality': row['properties']['hardware']['reality'] = 'simulated'
            if signal == 'provider': row['deviceProperties'] = {'provider': 'com.apple.CoreSimulator.SimulatorCoreDevicePlugin'}
            if signal == 'modern_provider': row['properties']['provider'] = 'com.apple.CoreSimulator.SimulatorCoreDevicePlugin'
            if signal == 'visibility': row['visibilityClass'] = 'simulators'
            if signal == 'modern_visibility': row['properties']['state']['visibilityClass'] = 'simulators'
            with self.subTest(signal=signal):
                record = ios.device_record(row)
                self.assertEqual(record['reality'], 'simulated')
                self.assertEqual(record['exclusion_reason'], 'simulated_device')
                self.assertFalse(record['available'])

    def test_device_actions_reject_simulator_before_any_mutation(self):
        args = self.launch(); args.destination = 'device'; args.pid = 42; args.app = self.root / 'does-not-exist.app'
        args.storage_id = 'a' * 24
        row = self.physical_record(); row['hardwareProperties']['reality'] = 'simulated'
        operations = [lambda: ios.install(args), lambda: ios.launch(args), lambda: ios.stop(args),
                      lambda: ios.transfer(args, True, 'input.json', self.root / 'input.json'),
                      lambda: ios.transfer(args, False, 'live.json', self.root / 'observation.json')]
        for operation in operations:
            with patch.object(ios, 'require_mac'), patch.object(ios, 'device_json', return_value=({'result': {'devices': [row]}}, None)) as command, \
                    patch.object(ios, 'run') as run, patch.object(ios, 'require_development_install') as development:
                with self.assertRaisesRegex(ValueError, 'simulated'): operation()
                command.assert_called_once_with(['list', 'devices'])
                run.assert_not_called(); development.assert_not_called()

    def test_physical_device_guard_rejects_missing_unknown_and_unsupported_targets(self):
        args = self.launch(); args.destination = 'device'
        cases = [([], 'not uniquely present')]
        for platform in ['macOS', 'tvOS', 'watchOS', None]:
            row = self.physical_record(); row['hardwareProperties']['platform'] = platform
            cases.append(([row], 'not a supported iOS'))
        unknown = self.physical_record(); del unknown['hardwareProperties']['reality']
        cases.append(([unknown], 'did not report hardware reality'))
        for rows, message in cases:
            with self.subTest(message=message), patch.object(ios, 'device_json', return_value=({'result': {'devices': rows}}, None)):
                with self.assertRaisesRegex(ValueError, message): ios.require_physical_device(args)
        self.assertEqual(ios.device_record(unknown)['exclusion_reason'], 'unverified_reality')

    def test_legacy_and_modern_physical_devices_pass_guard(self):
        args = self.launch(); args.destination = 'device'
        for modern in [False, True]:
            row = self.physical_record(modern=modern)
            with patch.object(ios, 'device_json', return_value=({'result': {'devices': [row]}}, None)):
                self.assertEqual(ios.require_physical_device(args)['reality'], 'physical')

    def test_bridge_scope(self):
        self.assertEqual(ios.bridge_path('a' * 24, 'results/123.json'), 'Library/Application Support/Langmake/' + 'a' * 24 + '/results/123.json')
        for identity, name in [('../bad', 'live.json'), ('a' * 24, '../../otherapp/file')]:
            with self.assertRaises(ValueError): ios.bridge_path(identity, name)

    def pull_args(self):
        return ios.parser().parse_args(['pull', '--destination', 'device', '--device', '12345678-1234-1234-1234-123456789012',
            '--bundle-id', 'org.example.fixture', '--storage-id', 'a' * 24, '--path', 'saves/optional.json', '--output', str(self.root / 'pulled.json')])

    def file_listing(self, names=()):
        return {'info': {'outcome': 'success', 'commandType': 'devicectl.device.info.files'},
                'result': {'deviceIdentifier': self.pull_args().device, 'domain': 'appDataContainer',
                           'domainIdentifier': 'org.example.fixture',
                           'files': [{'name': name, 'relativePath': name} for name in names]}}

    def pull_with_responses(self, responses, partial=b'partial'):
        args = self.pull_args()
        def command(arguments, check=True):
            self.assertFalse(check)
            if arguments[:3] == ['device', 'copy', 'from']:
                self.assertEqual(Path(args.output).read_bytes(), b'previous')
                Path(arguments[-1]).write_bytes(partial)
            return responses.pop(0)
        with patch.object(ios, 'require_mac'), patch.object(ios, 'require_physical_device'), \
                patch.object(ios, 'require_development_install'), patch.object(ios, 'apple_help'), \
                patch.object(ios, 'device_json', side_effect=command) as calls:
            return ios.transfer(args, False, args.path, args.output), calls.call_args_list

    def copy_failure(self, code=7000):
        return ({'info': {'outcome': 'failed'}, 'error': {'domain': 'com.apple.dt.CoreDeviceError', 'code': code}},
                subprocess.CompletedProcess([], 1, b'', b'copy failed'))

    def test_missing_app_file_requires_successful_scoped_parent_listing(self):
        args = self.pull_args(); Path(args.output).write_bytes(b'previous')
        responses = [self.copy_failure(), (self.file_listing(['actual-save.json']), subprocess.CompletedProcess([], 0, b'', b''))]
        with self.assertRaises(ios.AppFileNotFoundError) as error: self.pull_with_responses(responses)
        self.assertEqual(error.exception.path, args.path)
        self.assertEqual(error.exception.evidence, 'successful_parent_directory_listing')
        self.assertEqual(Path(args.output).read_bytes(), b'previous')
        self.assertFalse(list(self.root.glob('pulled.json.pull-*')))
        self.assertEqual(responses, [])

    def test_generic_copy_error_is_not_missing_when_file_exists(self):
        args = self.pull_args(); Path(args.output).write_bytes(b'previous')
        responses = [self.copy_failure(), (self.file_listing(['OPTIONAL.JSON']), subprocess.CompletedProcess([], 0, b'', b''))]
        with self.assertRaisesRegex(RuntimeError, 'Device copy failed') as error: self.pull_with_responses(responses)
        self.assertNotIsInstance(error.exception, ios.AppFileNotFoundError)
        self.assertEqual(Path(args.output).read_bytes(), b'previous')

    def test_connection_auth_and_listing_failures_are_not_file_absence(self):
        args = self.pull_args(); Path(args.output).write_bytes(b'previous')
        for code in [1001, 403, 7000]:
            responses = [self.copy_failure(code)]
            if code == 7000:
                responses.append(({'info': {'outcome': 'failed'}, 'error': {'code': 403, 'description': 'Device locked or unauthorized'}},
                                  subprocess.CompletedProcess([], 1, b'', b'connection failed')))
            with self.subTest(code=code), self.assertRaises(RuntimeError) as error: self.pull_with_responses(responses)
            self.assertNotIsInstance(error.exception, ios.AppFileNotFoundError)
            self.assertEqual(responses, [])
            self.assertEqual(Path(args.output).read_bytes(), b'previous')

    def test_unverified_directory_scope_or_entries_are_not_file_absence(self):
        cases = []
        for key, value in [('deviceIdentifier', 'another-device'), ('domainIdentifier', 'another.app'), ('domain', 'temporary'), ('files', None),
                           ('files', [{}]), ('files', [{'name': 'a', 'relativePath': 'a'}, {'name': 'A', 'relativePath': 'A'}])]:
            listing = self.file_listing(); listing['result'][key] = value; cases.append(listing)
        listing = self.file_listing(); listing['info']['commandType'] = 'unknown'; cases.append(listing)
        for listing in cases:
            with patch.object(ios, 'apple_help'), patch.object(ios, 'device_json', return_value=(listing, subprocess.CompletedProcess([], 0, b'', b''))):
                with self.assertRaises(RuntimeError): ios.device_file_absent(self.pull_args().device, 'org.example.fixture', 'saves/optional.json')

    def test_successful_pull_commits_bounded_output_without_listing(self):
        args = self.pull_args(); Path(args.output).write_bytes(b'previous')
        result, calls = self.pull_with_responses([({'info': {'outcome': 'success'}}, subprocess.CompletedProcess([], 0, b'', b''))], b'complete')
        self.assertTrue(result['ok']); self.assertEqual(len(calls), 1)
        self.assertEqual(Path(args.output).read_bytes(), b'complete')
        self.assertFalse(list(self.root.glob('pulled.json.pull-*')))

    def test_oversized_pull_does_not_replace_previous_output(self):
        args = self.pull_args(); Path(args.output).write_bytes(b'previous')
        with patch.object(ios, 'LIMIT', 3), self.assertRaisesRegex(ValueError, 'bounded regular file'):
            self.pull_with_responses([({'info': {'outcome': 'success'}}, subprocess.CompletedProcess([], 0, b'', b''))])
        self.assertEqual(Path(args.output).read_bytes(), b'previous')
        self.assertFalse(list(self.root.glob('pulled.json.pull-*')))

    def test_pull_cli_emits_explicit_file_not_found_only_for_verified_absence(self):
        args = ['pull', '--destination', 'device', '--device', self.pull_args().device, '--bundle-id', 'org.example.fixture',
                '--storage-id', 'a' * 24, '--path', 'saves/optional.json', '--output', str(self.root / 'out.json')]
        for error in [ios.AppFileNotFoundError('saves/optional.json', 'successful_parent_directory_listing'), RuntimeError('connection refused')]:
            stream = io.StringIO()
            with patch.object(ios, 'bridge', side_effect=error), patch.object(sys, 'stdout', stream):
                self.assertEqual(ios.main(args), 1)
            report = json.loads(stream.getvalue())
            self.assertEqual(report.get('code') == 'file_not_found', isinstance(error, ios.AppFileNotFoundError))

    def test_simulator_missing_file_is_explicit(self):
        args = self.pull_args(); args.destination = 'simulator'; container = self.root / 'container'; container.mkdir()
        with patch.object(ios, 'require_mac'), patch.object(ios, 'require_development_install'), patch.object(ios, 'simulator_container', return_value=container):
            with self.assertRaises(ios.AppFileNotFoundError): ios.transfer(args, False, args.path, args.output)

    def launch(self, *extra):
        return ios.parser().parse_args(['launch', '--destination', 'simulator', '--device', '12345678-1234-1234-1234-123456789012',
                                       '--bundle-id', 'org.example.fixture'] + list(extra))

    def test_timeline_tick_zero_and_bounds(self):
        args = self.launch('--steps', '120', '--input', 'timeline.json', '--ticks', '0,30,120', '--output-dir', 'evidence/run')
        self.assertEqual(ios.launch_arguments(args), ['--steps', '120', '--input', 'timeline.json', '--ticks', '0,30,120', '--output-dir', 'evidence/run'])
        for extra in [['--frames', '2'], ['--capture', 'shot.png']]:
            args = self.launch('--steps', '120', '--input', 'timeline.json', '--ticks', '0,30,120', '--output-dir', 'evidence/run', *extra)
            with self.assertRaises(ValueError): ios.launch_arguments(args)
        for ticks in ['2,1', '0,0', '121', '-1', '0;touch file', '٠,١']:
            args = self.launch('--steps', '120', '--input', 'timeline.json', '--ticks=' + ticks, '--output-dir', 'evidence/run')
            with self.assertRaises(ValueError): ios.launch_arguments(args)

    def test_partial_timeline_and_path_traversal_rejected(self):
        for arguments in [['--steps', '1'], ['--capture', 'shot.png'], ['--frames', '1', '--capture', '../escape.png'], ['--frames', '36001']]:
            with self.assertRaises(ValueError): ios.launch_arguments(self.launch(*arguments))

    def test_release_bridge_denied_before_transfer(self):
        args = self.launch('--development')
        with patch.object(ios, 'CACHE', self.root / 'cache'):
            with self.assertRaisesRegex(ValueError, 'debug app'): ios.require_development_install(args)
            ios.write_json(ios.install_receipt(args), {'development': False})
            with self.assertRaisesRegex(ValueError, 'debug app'): ios.require_development_install(args)
            ios.write_json(ios.install_receipt(args), {'development': True})
            ios.require_development_install(args)

    def test_control_uploads_commit_after_request(self):
        request = self.root / 'request.json'
        ios.write_json(request, {'id': '1234', 'session': 'fixture-session', 'action': 'step', 'steps': 2})
        args = argparse.Namespace(command='control', file=str(request), device='12345678', storage_id='a' * 24)
        calls = []
        def transfer(args, upload, relative, local):
            calls.append((relative, Path(local).read_bytes()))
            return {'ok': True}
        with patch.object(ios, 'transfer', transfer):
            report = ios.bridge(args)
        self.assertEqual([item[0] for item in calls], ['inbox/1234.json', 'inbox/1234.ready'])
        self.assertEqual(calls[1][1].decode().strip(), ios.digest(request))
        self.assertIsNone(report['executed'])

    def test_control_budget_and_numeric_id_enforced(self):
        request = self.root / 'request.json'
        args = argparse.Namespace(command='control', file=str(request), device='12345678')
        for document in [{'id': '../bad', 'session': 'x', 'action': 'step'}, {'id': '١٢٣', 'session': 'x', 'action': 'step'}, {'id': '1', 'session': 'x', 'action': 'x' * 20000}]:
            ios.write_json(request, document)
            with patch.object(ios, 'transfer') as transfer, self.assertRaises(ValueError): ios.bridge(args)
            transfer.assert_not_called()

    def test_json_output_precedes_app_passthrough_arguments(self):
        with patch.object(ios, 'require_mac'), patch.object(ios, 'run') as run:
            ios.device_json(['device', 'process', 'launch', '--device', '12345678', 'org.example.fixture', '--fresh'])
            command = run.call_args[0][0]
            self.assertEqual(command[:3], ['xcrun', 'devicectl', '--json-output'])
            self.assertEqual(command[-2:], ['org.example.fixture', '--fresh'])

    def test_device_launch_forwards_exact_native_arguments_after_bundle_identifier(self):
        scenarios = [[], ['--development', '--fresh', '--frames', '3', '--capture', 'phone.png'],
                     ['--steps', '120', '--input', 'timeline.json', '--ticks', '0,120', '--output-dir', 'evidence/run']]
        for flags in scenarios:
            with self.subTest(flags=flags):
                args = self.launch(*flags); args.destination = 'device'
                with patch.object(ios, 'require_mac'), patch.object(ios, 'require_development_install') as development, patch.object(ios, 'apple_help') as help_call, patch.object(ios, 'device_json') as command:
                    command.side_effect = [({'result': {'devices': [self.physical_record(args.device)]}}, None),
                                           ({'result': {'process': {'processIdentifier': 17}}}, None)]
                    result = ios.launch(args)
                self.assertEqual(result['pid'], 17)
                self.assertEqual(command.call_args[0][0], ['device', 'process', 'launch', '--device', args.device,
                                                          '--terminate-existing', 'org.example.fixture'] + flags)
                self.assertEqual(result['arguments'], flags)
                self.assertEqual('arguments' in help_call.call_args[0][1], bool(flags))
                self.assertEqual(development.call_count, 1 if flags else 0)

    def test_push_cannot_bypass_committed_command_delivery(self):
        for path in ['commands/1.json', 'inbox/1.json']:
            args = argparse.Namespace(command='push', path=path, file='ignored')
            with patch.object(ios, 'transfer') as transfer, self.assertRaisesRegex(ValueError, 'atomic'): ios.bridge(args)
            transfer.assert_not_called()

    def test_physical_upload_does_not_guess_directory_creation(self):
        args = argparse.Namespace(command='push', destination='device', path='nested/input.json', file='ignored')
        with patch.object(ios, 'transfer') as transfer, self.assertRaisesRegex(ValueError, 'filename'): ios.bridge(args)
        transfer.assert_not_called()

    def test_simulator_uses_workspace_device_set(self):
        with patch.object(ios, 'require_mac'):
            command = ios.simctl(['list', 'devices'])
        self.assertEqual(command[:3], ['xcrun', 'simctl', '--set'])
        self.assertEqual(Path(command[3]), ios.CACHE / 'simulators')

    def test_application_icon_stage_versions_and_cli_override(self):
        project = {'title': 'Metadata title', 'project_id': 'stable-game', 'default_profile': 'phone',
                   'application': {'id': 'org.example.metadata', 'label': 'My game', 'version': '1.2', 'build': 8,
                                   'icon': 'icon.png', 'orientation': 'portrait'}}
        self.write_project(project)
        source = self.cooked / 'icon.png'
        source.write_bytes(ios.mobile_assets.png_bytes(1024, b'\x20\x60\xa0' * 1024 * 1024))
        cook = ios.read_json(self.cooked / 'cook-manifest.json')
        cook['files'].append({'path': 'icon.png', 'bytes': source.stat().st_size, 'revision': ios.source_revision(source)})
        ios.write_json(self.cooked / 'cook-manifest.json', cook)
        output = self.root / 'metadata-stage'
        report = ios.stage_cooked(self.cooked, output, resources=self.resources, orientation='landscape')
        manifest = ios.verify_stage(output)
        self.assertEqual(report['bundle_id'], 'org.example.metadata')
        self.assertEqual((manifest['application']['version'], manifest['application']['build']), ('1.2', 8))
        self.assertEqual((report['orientation'], report['profile']), ('landscape', 'phone'))
        self.assertEqual(report['icon']['source_sha256'], ios.digest(source))
        self.assertTrue((output / 'runtime/application/Assets.xcassets/AppIcon.appiconset/Contents.json').is_file())
        self.assertEqual(report['storage_id'], ios.hashlib.sha256(b'stable-game').hexdigest()[:24])

    def test_native_cache_excludes_content_but_keys_engine_toolchain_and_arch(self):
        folder, _ = self.stage(); manifest = ios.verify_stage(folder)
        shaderc = self.root / 'shaderc'; shaderc.write_bytes(b'fixture')
        args = self.build_args()
        toolchain = {'checks': [{'name': 'Xcode', 'actual': '27.0'}]}
        first = ios.native_cache_key(args, manifest, toolchain, shaderc)
        changed = dict(manifest, label='New title', content_id='new', application={'build': 20, 'icon': 'other.png'})
        self.assertEqual(first, ios.native_cache_key(args, changed, toolchain, shaderc))
        self.assertNotEqual(first, ios.native_cache_key(args, manifest, {'checks': [{'name': 'Xcode', 'actual': '27.1'}]}, shaderc))
        self.assertNotEqual(first, ios.native_cache_key(self.build_args(arch='x86_64'), manifest, toolchain, shaderc))

    def signing_block_fixture(self):
        folder, _ = self.stage(); manifest = ios.verify_stage(folder)
        shaderc = self.root / 'shaderc'; shaderc.write_bytes(b'fixture')
        args = self.build_args(device='12345678', staged=folder, output=self.root / 'output', shaderc=str(shaderc), jobs=4)
        record = ios.device_record(self.physical_record(args.device, modern=True))
        commands = []
        def run(command, **kwargs):
            commands.append(command)
            if command[0] == 'xcodebuild':
                work = Path(command[command.index('-project') + 1]).parent
                app = work / 'Debug-iphoneos/LangmakePlayer.app'; app.mkdir(parents=True)
                ios.shutil.copytree(folder, app / 'langmake')
                with (app / 'Info.plist').open('wb') as stream:
                    ios.plistlib.dump({'CFBundleIdentifier': manifest['bundle_identifier'], 'CFBundleExecutable': 'LangmakePlayer',
                                      'CFBundleShortVersionString': manifest['application']['version'], 'CFBundleVersion': str(manifest['application']['build'])}, stream)
                (app / 'LangmakePlayer').write_bytes(b'compiled executable')
                (app / 'embedded.mobileprovision').write_bytes(b'existing profile')
                entitlements = work / 'app.xcent'; entitlements.write_bytes(b'<plist/>')
                diagnostic = ' '.join(ios.shlex.quote(str(item)) for item in ['/usr/bin/codesign', '--force', '--sign', 'a' * 40,
                                      '--entitlements', entitlements, '--generate-entitlement-der', app]) + '\nerrSecInternalComponent\n'
                return subprocess.CompletedProcess(command, 65, diagnostic.encode(), b'')
            return subprocess.CompletedProcess(command, 0, b'arm64\n' if command[:2] == ['xcrun', 'lipo'] else b'', b'')
        with patch.object(ios, 'require_mac'), patch.object(ios, 'require_physical_device', return_value=record), \
                patch.object(ios, 'doctor', return_value={'can_build': True}), patch.object(ios.os, 'access', return_value=True), \
                patch.object(ios, 'run', side_effect=run):
            report = ios.build(args)
        self.assertEqual(report['status'], 'blocked', report)
        self.assertTrue(report['resumable'], report)
        return report, commands

    def test_signing_resume_preserves_failed_attempt_and_never_recompiles(self):
        with patch.object(ios, 'CACHE', self.root / 'cache'):
            blocked, _ = self.signing_block_fixture()
            operation = ios.read_json(blocked['operation_path'])
            failed = Path(operation['attempts'][1]['path']); original = failed.read_bytes()
            helper = Path(blocked['helper']).read_bytes()
            self.assertTrue(helper.startswith(b'#!/bin/sh\n')); self.assertNotIn(b'\r', helper)
            commands = []
            def run(command, **kwargs):
                commands.append(command)
                return subprocess.CompletedProcess(command, 0, b'arm64\n' if command[:2] == ['xcrun', 'lipo'] else b'', b'')
            with patch.object(ios, 'require_mac'), patch.object(ios, 'run', side_effect=run):
                result = ios.resume(argparse.Namespace(operation=blocked['operation_id']))
            self.assertTrue(result['ok']); self.assertEqual(result['status'], 'complete')
            self.assertTrue(any(command[:2] == ['codesign', '--force'] for command in commands))
            self.assertFalse(any(command[0] in ['cmake', 'xcodebuild'] for command in commands))
            self.assertEqual(failed.read_bytes(), original)
            self.assertEqual(result['operation_id'], blocked['operation_id'])

    def test_signing_resume_rejects_changed_candidate_before_signing(self):
        with patch.object(ios, 'CACHE', self.root / 'cache'):
            blocked, _ = self.signing_block_fixture()
            operation = ios.read_json(blocked['operation_path'])
            (Path(operation['candidate']) / 'LangmakePlayer').write_bytes(b'changed binary')
            with patch.object(ios, 'require_mac'), patch.object(ios, 'run') as run, self.assertRaisesRegex(ValueError, 'candidate changed'):
                ios.resume(argparse.Namespace(operation=blocked['operation_id']))
            run.assert_not_called()

    def test_prerequisite_block_records_action_and_preserves_evidence(self):
        folder, _ = self.stage()
        args = self.build_args(destination='simulator', team_id=None, staged=folder, output=self.root / 'output', shaderc=None, jobs=4)
        report = {'can_build': False, 'checks': [{'name': 'Xcode first-launch setup', 'ok': False}]}
        with patch.object(ios, 'CACHE', self.root / 'cache'), patch.object(ios, 'require_mac'), patch.object(ios, 'doctor', return_value=report), patch.object(ios, 'run') as run:
            result = ios.build(args)
            self.assertEqual(result['status'], 'blocked'); self.assertEqual(result['blocking']['code'], 'apple_agreement_or_setup_required')
            self.assertFalse(result['resumable']); self.assertTrue(Path(result['operation_path']).is_file())
            run.assert_not_called()
            status = ios.inspect_operation(argparse.Namespace(operation=result['operation_id']))
            self.assertEqual(status['attempts'], 1)
            self.assertIn('next_step', status['blocking'])

    def test_operation_summary_bounds_diagnostics_without_mutating_evidence(self):
        message = ('Unrelated compiler output\n' * 400) + 'error: errSecInternalComponent\n'
        blocking = {'code': 'signing_interaction_required', 'message': message, 'next_step': 'Run the recorded local signing helper.'}
        operation = {'ok': False, 'status': 'blocked', 'blocking': blocking, 'helper': '/authorized/work/sign-local.sh'}
        summary = ios.operation_summary(operation)
        self.assertLessEqual(len(summary['blocking']['message']), 512)
        self.assertIn('verified app', summary['blocking']['message'])
        self.assertIn('errSecInternalComponent', summary['blocking']['message'])
        self.assertTrue(summary['blocking']['message_truncated'])
        self.assertEqual(summary['blocking']['next_step'], blocking['next_step'])
        self.assertEqual(summary['helper'], operation['helper'])
        self.assertEqual(operation['blocking']['message'], message)
        self.assertNotIn('message_truncated', operation['blocking'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
