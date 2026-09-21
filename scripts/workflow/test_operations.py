"""Offline workflow regression tests; no SDK, SSH host or device is contacted."""
import copy
import json
import os
from pathlib import Path
import shutil
import stat
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from scripts.workflow import common, targets, transport, operations


ANDROID = {'platform': 'android', 'serial': 'test-device', 'abi': 'arm64-v8a', 'profile': 'phone'}
IOS = {'platform': 'ios', 'destination': 'device', 'device': '12345678-1234-1234-1234-123456789abc', 'team_id': 'ABCDEFGHIJ'}


class WorkspaceCase(unittest.TestCase):
    def setUp(self):
        cache = common.ROOT / '.cache/workflow-unit'
        cache.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='case-', dir=cache)
        self.root = Path(self.temp.name)
        self.project = self.root / 'game'
        self.project.mkdir()
        common.write(self.project / 'project.json', {'title': 'Authored title', 'application': {'id': 'org.test.authored'}})
        self.engine = self.root / 'engine.exe'
        self.engine.write_bytes(b'fake engine - never executed')

    def tearDown(self):
        common.within(self.root, common.ROOT / '.cache/workflow-unit')
        self.temp.cleanup()

    def assertCode(self, code, call):
        with self.assertRaises(common.WorkflowError) as caught:
            call()
        self.assertEqual(caught.exception.code, code)
        return caught.exception


class CommonAndTargetTests(WorkspaceCase):
    def test_bounded_json(self):
        path = self.root / 'value.json'
        path.write_bytes(b'"abcdef"')
        self.assertCode('file_too_large', lambda: common.read(path, 3))

    def test_command_logs_are_immutable_with_binary_stdin(self):
        source = self.root / 'stdin.bin'
        source.write_bytes(b'input bytes')
        args = [sys.executable, '-c', 'import json,sys; print(json.dumps({"ok":True,"input":sys.stdin.buffer.read().decode()}))']
        one = common.command(args, self.root, 'same', stdin=source)
        two = common.command(args, self.root, 'same', stdin=source)
        self.assertNotEqual(one['stdout'], two['stdout'])
        self.assertEqual(common.read(one['stdout'])['input'], 'input bytes')
        self.assertEqual(common.read(two['stdout'])['input'], 'input bytes')

    def test_json_pointer_does_not_accept_negative_or_aliased_indices(self):
        self.assertEqual(common.pointer({'a/b': [3]}, '/a~1b/0'), 3)
        for pointer in ('/a~1b/-1', '/a~1b/00', '/a~2b'):
            with self.assertRaises(ValueError): common.pointer({'a/b': [3]}, pointer)

    def test_targets_are_bound_to_one_game(self):
        targets.add(self.project, 'phone', ANDROID)
        other = self.root / 'other'
        other.mkdir()
        common.write(other / '.langmake/targets.local.json', common.read(targets.config_path(self.project)))
        self.assertCode('target_setup_required', lambda: targets.load(other))

    def test_reject_malformed_target_fields(self):
        for extra in ({'serial': 3}, {'profile': []}, {'host': {'transport': 'local', 'alias': 'ignored'}}, {'register_device': 1}, {'configuration': 'debug\n'}):
            with self.subTest(extra=extra):
                self.assertCode('invalid_target', lambda: targets.validate(dict(ANDROID, **extra)))

    def test_simulator_cannot_register_or_sign(self):
        simulator = dict(IOS, destination='simulator')
        self.assertCode('invalid_target', lambda: targets.validate(simulator))
        del simulator['team_id']
        self.assertEqual(targets.validate(simulator), simulator)

    def test_ssh_requires_explicit_game_local_settings(self):
        target = dict(IOS, host={'transport': 'ssh', 'alias': 'mac', 'workspace': '/work'})
        self.assertCode('ssh_config_required', lambda: targets.validate(target))
        target['host']['config'] = str(self.root / 'config')
        self.assertCode('invalid_target', lambda: targets.validate(target))
        target['game_directory'] = 'games/test'
        self.assertEqual(targets.validate(target), target)

    def test_fingerprint_is_order_independent_and_sensitive(self):
        self.assertEqual(targets.fingerprint(ANDROID), targets.fingerprint(dict(reversed(list(ANDROID.items())))))
        self.assertNotEqual(targets.fingerprint(ANDROID), targets.fingerprint(dict(ANDROID, serial='other')))

    def test_deployment_lock_is_exclusive_and_releases(self):
        with operations.deployment_lock(self.project):
            with self.assertRaises(common.WorkflowError):
                with operations.deployment_lock(self.project): pass
        with operations.deployment_lock(self.project): pass


class ArchiveTests(WorkspaceCase):
    def archive(self, items):
        path = self.root / 'input.zip'
        with zipfile.ZipFile(path, 'w') as stream:
            for name, content in items: stream.writestr(name, content)
        return path

    def test_regular_archive(self):
        archive = self.archive([('project/project.json', '{}'), ('runtime/font.ttf', 'font')])
        output = self.root / 'output'
        operations.extract_stage_archive(archive, output, self.root)
        self.assertEqual((output / 'runtime/font.ttf').read_text(), 'font')

    def test_archive_rejects_traversal_aliases_and_parent_collisions_before_writing(self):
        cases = [[('../outside', '')], [('/absolute', '')], [('a/b', ''), ('a\\b', '')], [('a:b', '')], [('a/./b', '')],
                 [('A', ''), ('a', '')], [('a', ''), ('a/file', '')], [('caf\u00e9', ''), ('cafe\u0301', '')]]
        for index, members in enumerate(cases):
            with self.subTest(members=members):
                archive = self.archive(members)
                output = self.root / ('invalid-' + str(index))
                with self.assertRaises(ValueError): operations.extract_stage_archive(archive, output, self.root)
                self.assertFalse(output.exists())

    def test_archive_rejects_links(self):
        entry = zipfile.ZipInfo('link')
        entry.create_system = 3
        entry.external_attr = (stat.S_IFLNK | 0o777) << 16
        archive = self.archive([(entry, '/outside')])
        with self.assertRaises(ValueError): operations.extract_stage_archive(archive, self.root / 'output', self.root)


class TransportTests(WorkspaceCase):
    def setUp(self):
        super().setUp()
        self.adapter = transport.Platform(ANDROID, self.root / 'operation')
        self.live = {'running': True, 'session': 'session-a', 'source_revision': 'revision-a', 'published_at_ms': common.now()}

    def test_attempt_logs_are_unique_but_remote_stage_identity_is_stable(self):
        config = self.root / 'ssh-config'
        config.write_text('Host mac\n  HostName example.invalid\n')
        target = dict(IOS, game_directory='games/test', host={'transport':'ssh', 'alias':'mac', 'config':str(config), 'workspace':'/workspace'})
        one, two = transport.Platform(target, self.root / 'same'), transport.Platform(target, self.root / 'same')
        self.assertNotEqual(one.logs, two.logs)
        self.assertEqual(one.remote_folder, two.remote_folder)
        command = one.remote_command(['python3', '-c', 'print("$(unsafe)")'])
        self.assertIn("'print(\"$(unsafe)\")'", command)

    def test_live_requires_bounded_recent_typed_identity(self):
        self.assertTrue(transport.valid_live(self.live))
        for fields in ({'running': 1}, {'session': None}, {'source_revision': ''}, {'published_at_ms': True}, {'published_at_ms': common.now()-20000}):
            self.assertFalse(transport.valid_live(dict(self.live, **fields)))

    def test_reserved_identity_and_stale_observation_never_dispatch(self):
        with patch.object(self.adapter, 'call') as dispatch:
            self.assertCode('invalid_request', lambda: self.adapter.control('org.test.game', 'storage', self.live, 'step', session='other'))
            self.assertCode('stale_session', lambda: self.adapter.control('org.test.game', 'storage', dict(self.live, running=False), 'step'))
            dispatch.assert_not_called()

    def test_uncertain_delivery_is_dispatched_once_and_preserves_id(self):
        with patch.object(self.adapter, 'call', side_effect=TimeoutError('lost response')) as dispatch, patch.object(self.adapter, 'pull', return_value=None) as fetch, patch.object(transport.time,'monotonic',side_effect=[0,0,31]), patch.object(transport.time,'sleep'):
            error = self.assertCode('delivery_uncertain', lambda: self.adapter.control('org.test.game', 'storage', self.live, 'step'))
            self.assertTrue(error.details['request_id'].isdigit())
            self.assertEqual(dispatch.call_count, 1)
            self.assertEqual(fetch.call_count, 1)
            self.assertTrue(Path(error.details['request']).is_file())
            record=common.read(error.details['delivery_warning']['record'])
            self.assertEqual(record['warning']['exception_type'],'TimeoutError')
            self.assertEqual(record['dispatch_count'],1)

    def test_failed_copy_acknowledgment_accepts_delayed_verified_receipt_without_redispatch(self):
        failed={'exit_code':1,'data':{'ok':False,'error':'Failed to retrieve the file node'}}
        calls=[]
        def pull(bundle,storage,relative,output,**kwargs):
            calls.append(relative)
            return None if len(calls)==1 else self.receipt(output, action='pause')
        with patch.object(self.adapter,'call',return_value=failed) as dispatch,patch.object(self.adapter,'pull',side_effect=pull),patch.object(transport.time,'sleep'):
            result=self.adapter.control('org.test.game','storage',self.live,'pause')
        self.assertTrue(result['ok']);self.assertEqual(dispatch.call_count,1)
        self.assertEqual(calls,['results/'+result['request_id']+'.json']*2)
        self.assertEqual(result['delivery_warning']['resolved_by'],'verified_native_receipt')
        warning=common.read(result['delivery_warning']['record'])
        self.assertEqual(warning['warning']['result'],failed)
        self.assertEqual(warning['request_id'],result['request_id'])

    def test_dispatch_exception_can_be_resolved_by_receipt(self):
        with patch.object(self.adapter,'call',side_effect=TimeoutError('lost SSH response')) as dispatch,patch.object(self.adapter,'pull',side_effect=lambda b,s,p,out,**kw:self.receipt(out)):
            result=self.adapter.control('org.test.game','storage',self.live,'step')
        self.assertTrue(result['ok']);self.assertEqual(dispatch.call_count,1)
        self.assertEqual(common.read(result['delivery_warning']['record'])['warning']['message'],'lost SSH response')

    def test_failed_copy_acknowledgment_without_receipt_stays_uncertain(self):
        with patch.object(self.adapter,'call',return_value={'exit_code':1,'data':{'ok':False}}) as dispatch,patch.object(self.adapter,'pull',return_value=None) as fetch,patch.object(transport.time,'monotonic',side_effect=[0,0,31]),patch.object(transport.time,'sleep'):
            error=self.assertCode('delivery_uncertain',lambda:self.adapter.control('org.test.game','storage',self.live,'reset'))
        self.assertEqual(dispatch.call_count,1);self.assertEqual(fetch.call_count,1)
        self.assertTrue(Path(error.details['delivery_warning']['record']).is_file())

    def test_uncertain_dispatch_cannot_accept_mismatched_receipt(self):
        cases=[('id','wrong','receipt_mismatch'),('session','wrong','session_mismatch'),('action','reset','receipt_mismatch'),('source_revision','wrong','revision_mismatch')]
        for index,(field,value,code) in enumerate(cases):
            self.adapter=transport.Platform(IOS,self.root/('uncertain-'+str(index)))
            with patch.object(self.adapter,'call',return_value={'exit_code':1,'data':{'ok':False}}) as dispatch,patch.object(self.adapter,'pull',side_effect=lambda b,s,p,out,**kw:self.receipt(out,**{field:value})):
                self.assertCode(code,lambda:self.adapter.control('org.test.game','storage',self.live,'step'))
                self.assertEqual(dispatch.call_count,1)
                self.assertEqual(len(list(self.adapter.folder.glob('delivery-*.json'))),1)

    def test_uncertain_dispatch_preserves_warning_when_receipt_read_fails(self):
        with patch.object(self.adapter,'call',side_effect=TimeoutError('dispatch response lost')) as dispatch,patch.object(self.adapter,'pull',side_effect=OSError('device disconnected')):
            error=self.assertCode('receipt_observation_failed',lambda:self.adapter.control('org.test.game','storage',self.live,'step'))
        self.assertEqual(dispatch.call_count,1)
        self.assertTrue(Path(error.details['delivery_warning']['record']).is_file())

    def test_uncertain_capture_still_requires_complete_sidecar_binding(self):
        for index,updates in enumerate((None,{'session':'wrong'})):
            self.adapter=transport.Platform(IOS,self.root/('uncertain-capture-'+str(index)))
            with patch.object(self.adapter,'call',return_value={'exit_code':1,'data':{'ok':False}}) as dispatch,patch.object(self.adapter,'pull',side_effect=self.capture_pull(sidecar_updates=updates)):
                if updates:
                    self.assertCode('capture_mismatch',lambda:self.adapter.control('org.test.game','storage',self.live,'capture'))
                else:
                    result=self.adapter.control('org.test.game','storage',self.live,'capture')
                    self.assertEqual(result['result']['tick'],17)
                    self.assertEqual(result['delivery_warning']['resolved_by'],'verified_native_receipt')
                self.assertEqual(dispatch.call_count,1)

    def receipt(self, path, action='step', **overrides):
        request = common.read(next(self.adapter.folder.glob('request-*.json')))
        value = {'ok': True, 'id': request['id'], 'session': self.live['session'], 'source_revision': self.live['source_revision'], 'action': action}
        if action=='capture':value['image']='/private/game/evidence/'+request['id']+'.png'
        value.update(overrides)
        common.write(path, value)
        return path

    def test_receipt_must_match_id_session_action_and_revision(self):
        cases = [('id', 'wrong', 'receipt_mismatch'), ('session', 'wrong', 'session_mismatch'), ('action', 'reset', 'receipt_mismatch'), ('source_revision', 'wrong', 'revision_mismatch')]
        for index, (field, value, code) in enumerate(cases):
            self.adapter = transport.Platform(ANDROID, self.root / ('op-'+str(index)))
            with patch.object(self.adapter, 'call', return_value={'exit_code':0, 'data':{'ok':True}}) as dispatch, patch.object(self.adapter, 'pull', side_effect=lambda bundle, storage, relative, output, **kw: self.receipt(output, **{field:value})):
                self.assertCode(code, lambda: self.adapter.control('org.test.game', 'storage', self.live, 'step'))
                self.assertEqual(dispatch.call_count, 1)

    def test_receipt_transport_failure_preserves_uncertain_request(self):
        with patch.object(self.adapter, 'call', return_value={'exit_code':0, 'data':{'ok':True}}) as dispatch, patch.object(self.adapter, 'pull', side_effect=OSError('offline')):
            error=self.assertCode('receipt_observation_failed', lambda: self.adapter.control('org.test.game', 'storage', self.live, 'save', slot='progress'))
            self.assertTrue(error.details['request_id'])
            self.assertEqual(dispatch.call_count, 1)

    def test_successful_receipt(self):
        with patch.object(self.adapter, 'call', return_value={'exit_code':0, 'data':{'ok':True}}), patch.object(self.adapter, 'pull', side_effect=lambda b,s,p,out,**kw:self.receipt(out)):
            result=self.adapter.control('org.test.game','storage',self.live,'step')
            self.assertTrue(result['ok'])
            self.assertEqual(result['result']['session'], 'session-a')

    def test_malformed_receipt_never_replays_mutation(self):
        def pull(bundle,storage,relative,output,**kwargs):
            output.write_bytes(b'not json');return output
        with patch.object(self.adapter,'call',return_value={'exit_code':0,'data':{'ok':True}}) as dispatch,patch.object(self.adapter,'pull',side_effect=pull):
            self.assertCode('malformed_receipt',lambda:self.adapter.control('org.test.game','storage',self.live,'step'))
            self.assertEqual(dispatch.call_count,1)

    def test_capture_sidecar_checks_session_separately_from_receipt(self):
        def pull(bundle,storage,relative,output,**kwargs):
            if relative.startswith('results/'):return self.receipt(output,action='capture')
            if relative.endswith('.png.json'):common.write(output,{'source_revision':'revision-a','session':'wrong','tick':0})
            else:output.write_bytes(b'image')
            return output
        with patch.object(self.adapter,'call',return_value={'exit_code':0,'data':{'ok':True}}),patch.object(self.adapter,'pull',side_effect=pull):
            self.assertCode('capture_mismatch',lambda:self.adapter.control('org.test.game','storage',self.live,'capture'))

    def capture_pull(self, sidecar_updates=None, receipt_updates=None):
        def pull(bundle,storage,relative,output,**kwargs):
            request=common.read(next(self.adapter.folder.glob('request-*.json')))
            image='/private/game/evidence/'+request['id']+'.png'
            if relative.startswith('results/'):
                value={'ok':True,'id':request['id'],'source_revision':'revision-a','image':image}
                value.update(receipt_updates or {});common.write(output,value)
            elif relative.endswith('.png.json'):
                value={'ok':True,'session':'session-a','source_revision':'revision-a','tick':17,'image':image}
                value.update(sidecar_updates or {});common.write(output,value)
            else:output.write_bytes(b'image')
            return output
        return pull

    def test_legacy_capture_uses_mandatory_sidecar_binding(self):
        with patch.object(self.adapter,'call',return_value={'exit_code':0,'data':{'ok':True}}) as dispatch,patch.object(self.adapter,'pull',side_effect=self.capture_pull()) as fetch:
            result=self.adapter.control('org.test.game','storage',self.live,'capture')
            self.assertEqual(fetch.call_count,3)
            self.assertEqual(dispatch.call_count,1)
            self.assertEqual(result['result']['session'],'session-a')
            self.assertEqual(result['result']['action'],'capture')
            self.assertEqual(result['result']['tick'],17)

    def test_legacy_capture_rejects_wrong_sidecar_session_revision_tick_or_image(self):
        for index,change in enumerate(({'session':'old'},{'source_revision':'old'},{'tick':True},{'tick':-1},{'image':'/private/game/evidence/other.png'})):
            with self.subTest(change=change):
                self.adapter=transport.Platform(ANDROID,self.root/('legacy-'+str(index)))
                with patch.object(self.adapter,'call',return_value={'exit_code':0,'data':{'ok':True}}) as dispatch,patch.object(self.adapter,'pull',side_effect=self.capture_pull(sidecar_updates=change)) as fetch:
                    self.assertCode('capture_mismatch',lambda:self.adapter.control('org.test.game','storage',self.live,'capture'))
                    self.assertEqual(fetch.call_count,3);self.assertEqual(dispatch.call_count,1)

    def test_optional_capture_receipt_fields_must_match_if_present(self):
        cases=[({'session':'old'},'session_mismatch'),({'action':'reset'},'receipt_mismatch'),({'image':'/private/game/evidence/other.png'},'capture_mismatch'),({'source_revision':'old'},'revision_mismatch')]
        for index,(change,code) in enumerate(cases):
            with self.subTest(change=change):
                self.adapter=transport.Platform(ANDROID,self.root/('capture-receipt-'+str(index)))
                with patch.object(self.adapter,'call',return_value={'exit_code':0,'data':{'ok':True}}),patch.object(self.adapter,'pull',side_effect=self.capture_pull(receipt_updates=change)) as fetch:
                    self.assertCode(code,lambda:self.adapter.control('org.test.game','storage',self.live,'capture'))
                    self.assertEqual(fetch.call_count,1)

    def test_ordinary_receipts_still_require_session_and_action(self):
        for index,key in enumerate(('session','action')):
            self.adapter=transport.Platform(ANDROID,self.root/('ordinary-'+str(index)))
            def pull(bundle,storage,relative,output,**kwargs):
                self.receipt(output);value=common.read(output);del value[key];common.write(output,value);return output
            with patch.object(self.adapter,'call',return_value={'exit_code':0,'data':{'ok':True}}),patch.object(self.adapter,'pull',side_effect=pull):
                self.assertCode('session_mismatch' if key=='session' else 'receipt_mismatch',lambda:self.adapter.control('org.test.game','storage',self.live,'step'))

    def test_failed_pull_is_not_confused_with_absent_receipt(self):
        stderr=self.root/'failure.log';stderr.write_text('device not found')
        with patch.object(self.adapter,'call',return_value={'exit_code':1,'data':None,'stderr':str(stderr)}):
            self.assertCode('device_read_failed',lambda:self.adapter.pull('org.test.game','storage','receipt.json',self.root/'out.json',missing_ok=True))
        stderr.write_text('No such file or directory')
        with patch.object(self.adapter,'call',return_value={'exit_code':1,'data':None,'stderr':str(stderr)}):
            self.assertIsNone(self.adapter.pull('org.test.game','storage','receipt.json',self.root/'out.json',missing_ok=True))

    def test_structured_missing_file_requires_explicit_optional_read(self):
        result = {'exit_code': 1, 'data': {'ok': False, 'code': 'file_not_found', 'evidence': 'successful_parent_directory_listing'}}
        with patch.object(self.adapter, 'call', return_value=result):
            self.assertIsNone(self.adapter.pull('org.test.game', 'storage', 'receipt.json', self.root/'out.json', missing_ok=True))
            self.assertCode('device_read_failed', lambda: self.adapter.pull('org.test.game', 'storage', 'receipt.json', self.root/'out.json'))

    def test_generic_coredevice_or_authorization_failure_is_not_absent(self):
        for message in ['Failed to retrieve the file node (com.apple.dt.CoreDeviceError error 7000)', 'Device connection unavailable', 'Permission denied']:
            result = {'exit_code': 1, 'data': {'ok': False, 'error': message}}
            with patch.object(self.adapter, 'call', return_value=result):
                self.assertCode('device_read_failed', lambda: self.adapter.pull('org.test.game', 'storage', 'receipt.json', self.root/'out.json', missing_ok=True))

    def test_fresh_live_rejects_prelaunch_session_and_wrong_revision(self):
        values=[{'exit_code':0,'data':{'ok':True,'live':self.live}}, {'exit_code':0,'data':{'ok':True,'live':dict(self.live,session='new')}}]
        with patch.object(self.adapter,'inspect',side_effect=values) as observe,patch.object(transport.time,'sleep'):
            result=self.adapter.fresh_live('org.test.game','storage',previous='session-a',revision='revision-a')
            self.assertEqual(result['session'],'new');self.assertEqual(observe.call_count,2)
        with patch.object(self.adapter,'inspect',return_value=values[-1]):
            self.assertCode('revision_mismatch',lambda:self.adapter.fresh_live('org.test.game','storage',revision='other'))


class OperationTests(WorkspaceCase):
    def setUp(self):
        super().setUp()
        self.target=copy.deepcopy(ANDROID)
        targets.add(self.project,'phone',self.target)
        self.calls=[]
        self.native_calls=[]
        self.restore_ok=True
        self.compatible=True
        self.existing_save=False
        self.fail_observe=False
        self.fail_install=False
        self.fail_save=False
        owner=self

        class FakePlatform:
            def __init__(self,target,folder):
                self.target=target;self.folder=folder;self.remote=False
            def base(self):return ['--serial',self.target.get('serial','test-device')]
            def identity(self,identifier):return ['--application-id',identifier]
            def call(self,args,**kwargs):
                owner.calls.append(list(args))
                if args[0]=='build':
                    output=Path(args[args.index('--output')+1]);output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(b'apk')
                    data={'ok':True,'apk':str(output),'storage_id':'storage','packaged_revision':'packaged','signed':True}
                elif args[0]=='install':
                    if owner.fail_install:raise TimeoutError('install response was lost')
                    data={'ok':True}
                else:data={'ok':True}
                return {'exit_code':0,'data':data}
            def inspect(self,identifier,storage):
                owner.calls.append(['inspect',identifier,storage])
                return {'exit_code':0,'data':{'ok':True,'live':{'session':'before','running':True,'source_revision':'old-package','published_at_ms':common.now()}}}
            def control(self,*args,**kwargs):
                owner.calls.append(['control',args[3]])
                if owner.fail_save:raise common.WorkflowError('delivery_uncertain','save was sent once')
                return {'ok':True,'receipt':'receipt.json'}
            def pull(self,identifier,storage,path,output,missing_ok=False):
                owner.calls.append(['pull',path])
                if not owner.existing_save and missing_ok:return None
                common.write(output,{'source_revision':'old-package','state':{'score':7}})
                return output
            def launch(self,identifier):
                owner.calls.append(['launch',identifier]);return {'ok':True}
            def fresh_live(self,identifier,storage,**kwargs):
                owner.calls.append(['fresh_live',kwargs])
                if owner.fail_observe:raise common.WorkflowError('observation_timeout','test observation unavailable')
                return {'mode':'play','session':'after','source_revision':'packaged'}

        self.platform_patch=patch.object(operations,'Platform',FakePlatform);self.platform_patch.start();self.addCleanup(self.platform_patch.stop)
        self.native_patch=patch.object(operations,'native',side_effect=self.native);self.native_patch.start();self.addCleanup(self.native_patch.stop)

    def native(self,engine,args,folder,label,**kwargs):
        self.native_calls.append(list(args))
        data={'ok':True}
        if args[0]=='inspect':
            data.update(revision='selected-source',application={'id':'org.test.selected','label':'Selected Profile Label'},resolved={'title':'Selected Title'})
        elif args[0]=='cook':
            output=Path(args[args.index('--output')+1]);output.mkdir(parents=True);common.write(output/'project.json',{})
            data['packaged_revision']='packaged'
        elif args[0]=='save-check':data.update(compatible=self.compatible,status='exact' if self.compatible else 'migration_required')
        elif args[0]=='simulate' and not self.restore_ok:return {'exit_code':1,'data':{'ok':False,'diagnostic':'no migrate hook'}}
        return {'exit_code':0,'data':data}

    def prior(self,configuration='debug'):
        target=dict(self.target,configuration=configuration)
        common.write(self.project/'.langmake/deployments/phone.json',{'id':'old','application_id':'org.test.selected','storage_id':'storage','packaged_revision':'old-package','target_config':target,'target_sha256':targets.fingerprint(target)})

    def deploy(self,stop='launch'):
        return operations.initial(self.project,self.engine,'phone',stop_after=stop)

    def test_selected_application_and_profile_are_used(self):
        result=self.deploy('build');self.assertTrue(result['ok'])
        build=next(c for c in self.calls if c[0]=='build')
        self.assertEqual(build[build.index('--application-id')+1],'org.test.selected')
        self.assertEqual(build[build.index('--label')+1],'Selected Profile Label')
        self.assertEqual(build[build.index('--profile')+1],'phone')
        self.assertFalse(any(c[0]=='install' for c in self.calls))

    def test_deployment_records_all_provenance_and_new_session(self):
        result=self.deploy();self.assertTrue(result['ok'])
        deployment=common.read(self.project/'.langmake/deployments/phone.json')
        self.assertEqual(deployment['source_revision'],'selected-source')
        self.assertEqual(deployment['engine_sha256'],common.sha(self.engine))
        self.assertEqual(deployment['target_sha256'],targets.fingerprint(self.target))
        observe=next(c for c in self.calls if c[0]=='fresh_live')
        self.assertEqual(observe[1]['previous'],'before')
        self.assertTrue(common.read(operations.play_record(self.project))['active'])

    def test_observation_failure_preserves_human_session_and_resume_does_not_relaunch(self):
        self.fail_observe=True
        result=self.deploy();self.assertEqual(result['status'],'blocked')
        self.assertTrue(common.read(operations.play_record(self.project))['active'])
        self.fail_observe=False
        resumed=operations.resume(self.project,result['operation_id'])
        self.assertTrue(resumed['ok'])
        self.assertEqual(sum(c[0]=='launch' for c in self.calls),1)
        folder=Path(result['record']).parent
        self.assertEqual(len(list(folder.glob('attempt-*/operation-result.json'))),2)

    def test_uncertain_install_cannot_be_replayed(self):
        self.fail_install=True
        result=self.deploy()
        self.assertEqual(result['status'],'blocked')
        self.assertCode('mutation_uncertain',lambda:operations.resume(self.project,result['operation_id']))
        self.assertEqual(sum(c[0]=='install' for c in self.calls),1)

    def test_uncertain_save_cannot_be_replayed(self):
        self.prior();self.fail_save=True
        result=self.deploy()
        self.assertEqual(result['status'],'blocked')
        self.assertCode('mutation_uncertain',lambda:operations.resume(self.project,result['operation_id']))
        self.assertEqual(sum(c[0]=='control' for c in self.calls),1)

    def test_incompatible_save_blocks_install_and_retry_does_not_resave(self):
        self.prior();self.existing_save=True;self.compatible=False;self.restore_ok=False
        result=self.deploy()
        self.assertEqual(result['blocking']['code'],'save_incompatible')
        self.assertFalse(any(c[0]=='install' for c in self.calls))
        self.restore_ok=True
        self.assertTrue(operations.resume(self.project,result['operation_id'])['ok'])
        self.assertEqual(sum(c[0]=='control' for c in self.calls),1)

    def test_explicit_migration_is_verified_before_install(self):
        self.prior();self.existing_save=True;self.compatible=False
        result=self.deploy();self.assertTrue(result['ok'])
        state=common.read(result['record'])
        checks=state['phases']['save_compatibility']['result']['checks']
        self.assertTrue(all(item['migration_verified'] for item in checks))
        save_check=next(c for c in self.native_calls if c[0]=='save-check')
        self.assertIn('compatibility-game',str(save_check[2]))
        self.assertNotIn('--profile',save_check)

    def test_same_device_alias_cannot_interrupt_human(self):
        targets.add(self.project,'alias',self.target)
        common.write(operations.play_record(self.project),{'active':True,'target':'phone','operation_id':'old','target_config':self.target})
        self.assertCode('human_session_active',lambda:operations.initial(self.project,self.engine,'alias'))
        self.assertTrue(operations.initial(self.project,self.engine,'alias',stop_after='build')['ok'])

    def test_release_launch_is_honest_about_missing_observation(self):
        target=dict(self.target,configuration='release');targets.remove(self.project,'phone');targets.add(self.project,'phone',target)
        result=self.deploy();self.assertTrue(result['ok'])
        observed=common.read(result['record'])['phases']['observe']['result']
        self.assertFalse(observed['verified_live'])
        self.assertFalse(any(c[0]=='fresh_live' for c in self.calls))

    def test_changed_release_cannot_skip_progress_compatibility(self):
        self.prior(configuration='release')
        result=self.deploy()
        self.assertEqual(result['blocking']['code'],'progress_unverifiable')
        self.assertFalse(any(c[0]=='install' for c in self.calls))

    def test_completed_resume_is_read_only(self):
        result=self.deploy();count=len(self.calls)
        self.assertTrue(operations.resume(self.project,result['operation_id'])['ok'])
        self.assertEqual(len(self.calls),count)

    def test_changed_source_or_target_cannot_resume(self):
        self.fail_observe=True;result=self.deploy()
        targets.remove(self.project,'phone');targets.add(self.project,'phone',dict(self.target,serial='another'))
        self.assertCode('target_changed',lambda:operations.resume(self.project,result['operation_id']))

    def test_windows_resource_adapter_runs_before_executable_fingerprint(self):
        targets.add(self.project,'pc',{'platform':'windows'})
        for directory in ('shaders','fonts'):
            (self.engine.parent/directory).mkdir();(self.engine.parent/directory/'fixture').write_bytes(b'resource')
        def apply(args,*unused,**kwargs):
            self.assertEqual(args[2],'apply')
            self.assertEqual(args[args.index('--source-executable')+1],str(self.engine))
            executable=Path(args[args.index('--executable')+1]);executable.write_bytes(executable.read_bytes()+b'icon resource')
            return {'exit_code':0,'data':{'ok':True}}
        with patch.object(operations,'command',side_effect=apply) as resource:
            result=operations.initial(self.project,self.engine,'pc',stop_after='build')
        self.assertTrue(result['ok']);self.assertEqual(resource.call_count,1)
        built=common.read(result['record'])['phases']['build']['result']
        self.assertEqual(built['executable_sha256'],common.sha(Path(built['directory'])/'runtime/langmake.exe'))
        self.assertNotEqual(built['executable_sha256'],common.sha(self.engine))

    def test_signing_resume_preserves_stage_and_compact_native_block(self):
        targets.add(self.project,'iphone',IOS)
        owner=self
        class SigningPlatform(operations.Platform):
            def call(self,args,**kwargs):
                owner.calls.append(list(args))
                if args[0]=='build':
                    return {'exit_code':2,'data':{'ok':False,'operation_id':'native-signing','operation_path':'/selected/mac/op.json','status':'blocked','resumable':True,'blocking':{'code':'signing_interaction_required','next_step':'Run helper locally'},'helper':'/selected/mac/sign-local.sh'}}
                if args[0]=='resume':
                    return {'exit_code':0,'data':{'ok':True,'app':'/selected/mac/game.app','operation_id':'native-signing','status':'complete','resumable':False,'storage_id':'storage','packaged_revision':'packaged'}}
                raise AssertionError(args)
        def stage(args,*unused,**kwargs):
            self.calls.append(['stage'])
            output=Path(args[args.index('--output')+1]);output.mkdir()
            return {'exit_code':0,'data':{'ok':True,'staged':str(output)}}
        with patch.object(operations,'Platform',SigningPlatform),patch.object(operations,'command',side_effect=stage):
            first=operations.initial(self.project,self.engine,'iphone',stop_after='build')
            self.assertEqual(first['status'],'blocked')
            self.assertTrue(first['native_operation']['resumable'])
            self.assertNotIn('result',first['blocking'])
            second=operations.resume(self.project,first['operation_id'])
        self.assertTrue(second['ok'])
        self.assertEqual(sum(c[0]=='stage' for c in self.calls),1)
        self.assertEqual(sum(c[0]=='build' for c in self.calls),1)
        self.assertEqual(sum(c[0]=='resume' for c in self.calls),1)
        snapshots=sorted(Path(first['record']).parent.glob('attempt-*/operation-result.json'))
        self.assertEqual(common.read(snapshots[0])['status'],'blocked')
        self.assertEqual(common.read(snapshots[1])['status'],'complete')


if __name__=='__main__':unittest.main()
