"""Scenario schema, state comparisons, and isolated-session regression tests."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
import itertools
from unittest.mock import patch
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from scripts.workflow.common import ROOT, WorkflowError, write, read, sha
from scripts.workflow import verify, targets, operations, workflow
from scripts.workflow.workflow import main


class VerificationTests(unittest.TestCase):
    def setUp(self):
        cache=ROOT/'.cache/workflow-unit-tests';cache.mkdir(parents=True,exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(dir=cache);self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
        write(self.root/'project.json',{'schema_version':2,'scenarios':{'smoke':'tests/smoke.json'}})
        write(self.root/'tests/smoke.json',{'steps':12,'input':{'events':[]},'capture_ticks':[0,12]})

    def test_scenario_registered_only_and_project_containment(self):
        self.assertEqual(verify.scenario(self.root,'smoke')['steps'],12)
        with self.assertRaises(WorkflowError):verify.scenario(self.root,'unregistered')
        write(self.root/'tests/smoke.json',{'input':'../secret.json'})
        with self.assertRaises(WorkflowError) as error:verify.scenario(self.root,'smoke')
        self.assertEqual(error.exception.code,'path_outside_workspace')

    def test_bounded_invalid_scenarios(self):
        for item in [{'steps':True},{'steps':36001},{'capture_ticks':[2,1]},{'capture_ticks':[0,0]},
                     {'tolerances':{'absolute':float('nan')}},{'save_roundtrip':'yes'},{'unknown':1},
                     {'assertions':[{'path':'state/x','equals':1}]},{'assertions':[{'path':'/x','min':True}]},
                     {'steps':[]},{'viewport':{'width':True}},{'viewport':{'density':float('inf')}},
                     {'viewport':{'safe_area':[0,0,9000,10]}},{'assertions':[{'path':'/bad~2escape','exists':False}]}]:
            with self.subTest(item=item):
                write(self.root/'tests/smoke.json',item)
                with self.assertRaises(WorkflowError):verify.scenario(self.root,'smoke')

    def test_assertions_missing_and_type_safety(self):
        result=verify.assertions({'state':{'score':3,'flag':True,'a/b':7}},[
            {'path':'/state/score','min':2,'max':3},{'path':'/state/missing','exists':False},
            {'path':'/state/a~1b','equals':7},{'path':'/state/flag','equals':1},
            {'path':'/state/missing','equals':None}])
        self.assertEqual([r['ok'] for r in result],[True,True,True,False,False])

    def test_recursive_numeric_comparisons_are_bounded(self):
        self.assertEqual(verify.differences({'x':[1.0]},{'x':[1.0000001]}),[])
        self.assertTrue(verify.differences({'x':True},{'x':1}))
        self.assertTrue(verify.differences({'a':1},{'b':1}))
        self.assertEqual(len(verify.differences(list(range(1000)),[-1]*1000)),64)
        self.assertTrue(verify.differences(float('nan'),float('nan')))

    def test_viewport_preserves_explicit_units(self):
        self.assertEqual(verify.viewport_args({'width':1280,'density':2,'safe_area':[20,0,1200,700]}),
                         ['--width','1280','--density','2','--safe-area','20,0,1200,700'])
        with self.assertRaises(WorkflowError):verify.viewport_args({'safe_area':[1,2]})

    def test_no_mobile_launch_without_distinct_verification_identity(self):
        target={'platform':'ios','destination':'device','device':'abcdef1234','team_id':'AAAAAAAAAA','role':'human'}
        with patch.object(verify,'selected',return_value=target),patch.object(verify,'Platform') as adapter:
            with self.assertRaises(WorkflowError):verify.mobile_captures(self.root,'phone',self.root,{},self.root/'input.json')
            adapter.assert_not_called()

    def test_cli_target_requires_explicit_setup(self):
        result=main(['target','list','--project',str(self.root)])
        self.assertTrue(result['ok']);self.assertEqual(result['targets'],{})
        result=main(['target','add','--project',str(self.root),'--name','phone'])
        self.assertFalse(result['ok']);self.assertEqual(result['code'],'target_file_required')


class MobileVerificationTests(unittest.TestCase):
    def setUp(self):
        cache=ROOT/'.cache/workflow-unit-tests';cache.mkdir(parents=True,exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(dir=cache);self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
        self.folder=self.root/'run-unique';self.folder.mkdir()
        self.engine=self.root/'engine.exe';self.engine.write_bytes(b'not executable')
        write(self.root/'project.json',{'schema_version':2,'title':'Game','application':{'id':'org.test.human'},'default_profile':'phone','profiles':{'phone':{}},'scenarios':{'smoke':'smoke.json'}})
        write(self.root/'smoke.json',{'steps':12,'capture_ticks':[0,12]})
        self.timeline=self.folder/'timeline.json';write(self.timeline,{'events':[]})
        self.target={'platform':'ios','destination':'device','device':'abcdef1234','team_id':'AAAAAAAAAA','role':'verification','profile':'phone',
                     'application_id':'org.test.human','verification_application_id':'org.test.verify','game_directory':'games/this-game',
                     'host':{'transport':'ssh','alias':'explicit-mac','config':str(self.root/'ssh-config'),'workspace':'/selected/workspace'}}
        targets.add(self.root,'verify',self.target)
        self.deployment={'target_config':self.target,'target_sha256':targets.fingerprint(self.target),'source_revision':'authored',
                         'engine_sha256':sha(self.engine),'storage_id':'storage','application_id':'org.test.verify','packaged_revision':'packaged'}
        self.deployment_path=self.root/'.langmake/deployments/verify.json';write(self.deployment_path,self.deployment)
        self.spec=verify.scenario(self.root,'smoke')
        self.responses=['new'];self.final_updates={};self.sidecar_updates={};self.calls=[];self.pulls=[];self.override_captures=None
        owner=self
        class FakePlatform:
            def __init__(self,target,folder):self.target=target;self.folder=folder;self.remote=True;self.poll=0;self.output=None
            def prepare(self):owner.calls.append(('prepare',))
            def inspect(self,*args):return {'exit_code':0,'data':{'ok':True,'live':{'session':'before-bootstrap'}}}
            def launch(self,bundle,flags):
                owner.calls.append(('launch',bundle,list(flags)))
                if '--output-dir' in flags:
                    self.output=flags[flags.index('--output-dir')+1]
                    if not self.output.startswith('/'):self.output='/app/private/storage/'+self.output
                return {'ok':True}
            def fresh_live(self,*args,**kwargs):
                owner.calls.append(('fresh_live',kwargs))
                return {'session':'bootstrap','source_revision':'packaged','storage_directory':'/app/private/storage'}
            def push(self,bundle,storage,path,source):
                owner.calls.append(('push',path));return {'ok':True}
            def pull(self,bundle,storage,relative,output,missing_ok=False):
                owner.pulls.append((relative,str(output)))
                if output.exists():raise AssertionError('Evidence path reused')
                if relative=='launch-result.json':
                    kind=owner.responses[min(self.poll,len(owner.responses)-1)];self.poll+=1
                    if kind=='none':return None
                    if kind=='running':data={'ok':True,'running':True}
                    elif kind=='fail':data={'ok':False,'diagnostic':'script failed on device'}
                    elif kind=='malformed':data=[]
                    else:
                        base=self.output if kind=='new' else '/app/private/storage/evidence/scenario-previous-run'
                        state={'ok':True,'session':'scenario-session','running':False,'source_revision':'packaged','tick':12,'state':{'won':True}}
                        state.update(owner.final_updates)
                        paths=[base+'/tick-'+str(t)+'.png' for t in owner.spec['capture_ticks']]
                        data={'ok':True,'captures':owner.override_captures or paths,'state':state}
                elif relative.endswith('.png.json'):
                    tick=int(Path(relative).name.split('-')[1].split('.')[0])
                    data={'ok':True,'session':'scenario-session','source_revision':'packaged','tick':tick,'width':800,'height':600,'ui_renderer':{'density':2,'safe_area':[10,10,780,580]}}
                    data.update(owner.sidecar_updates)
                else:
                    output.write_bytes(b'PNG fixture');return output
                write(output,data);return output
            def call(self,args):return {'exit_code':0,'data':{'ok':True,'devices':[{'serial':'explicit-phone','is_emulator':False}]}}
            def control(self,*args,**kwargs):
                owner.calls.append(('control',args[3]));return {'ok':True,'result':{'session':'bootstrap','tick':1,'source_revision':'packaged'}}
        self.adapter=patch.object(verify,'Platform',FakePlatform);self.adapter.start();self.addCleanup(self.adapter.stop)
        self.fake_platform=FakePlatform
        self.clock=patch.object(verify.time,'monotonic',side_effect=itertools.count(0,1));self.clock.start();self.addCleanup(self.clock.stop)
        self.sleep=patch.object(verify.time,'sleep');self.sleep.start();self.addCleanup(self.sleep.stop)

    def execute(self):return verify.mobile_captures(self.root,'verify',self.folder,self.spec,self.timeline,'authored',sha(self.engine))

    def assertCode(self,code,call):
        with self.assertRaises(WorkflowError) as error:call()
        self.assertEqual(error.exception.code,code)

    def test_remote_mobile_complete_result_uses_wrapped_state_and_nonce(self):
        captures,evidence=self.execute()
        self.assertEqual(evidence,'physically_rendered');self.assertEqual([c['tick'] for c in captures],[0,12])
        self.assertEqual(read(self.folder/'native-final-state.json')['session'],'scenario-session')
        self.assertEqual(sum(c[0]=='launch' for c in self.calls),2)
        self.assertEqual(next(c for c in self.calls if c[0]=='fresh_live')[1]['previous'],'before-bootstrap')

    def test_delayed_old_result_is_ignored_without_relaunch(self):
        self.responses=['old','running','new']
        captures,_=self.execute();self.assertEqual(len(captures),2)
        results=[local for remote,local in self.pulls if remote=='launch-result.json']
        self.assertEqual(len(results),3);self.assertEqual(len(set(results)),3)
        self.assertEqual(sum(c[0]=='launch' for c in self.calls),2)

    def test_wrong_result_tick_revision_or_session_is_rejected(self):
        for field,value in [('tick',True),('tick',11),('source_revision','old'),('session','bootstrap'),('session',''),('running',True)]:
            with self.subTest(field=field,value=value):
                self.final_updates={field:value};self.folder=self.root/('case-'+field+'-'+str(value));self.folder.mkdir()
                self.assertCode('scenario_mismatch',self.execute)

    def test_wrong_capture_tick_revision_or_session_is_rejected(self):
        for field,value in [('tick',True),('tick',11),('source_revision','old'),('session','other')]:
            with self.subTest(field=field,value=value):
                self.sidecar_updates={field:value};self.folder=self.root/('capture-'+field+'-'+str(value));self.folder.mkdir()
                self.assertCode('capture_mismatch',self.execute)

    def test_app_failure_is_reported_without_retry(self):
        self.responses=['fail'];self.assertCode('native_scenario_failed',self.execute)
        self.assertEqual(sum(c[0]=='launch' for c in self.calls),2)

    def test_malformed_result_is_not_a_snapshot(self):
        self.responses=['malformed'];self.assertCode('malformed_result',self.execute)

    def test_old_nonce_timeout_never_replays_launch(self):
        self.responses=['old'];self.assertCode('scenario_timeout',self.execute)
        self.assertEqual(sum(c[0]=='launch' for c in self.calls),2)

    def test_no_device_contact_when_source_runtime_or_target_is_stale(self):
        for key,value,code in [('source_revision','old','deployment_stale'),('engine_sha256','old','runtime_changed'),('target_sha256','old','target_changed')]:
            with self.subTest(key=key):
                write(self.deployment_path,dict(self.deployment,**{key:value}));self.assertCode(code,self.execute)
        self.assertEqual(self.calls,[])

    def test_protected_same_device_alias_blocks_even_distinct_app(self):
        human=dict(self.target,role='human',application_id='org.test.another-human-app')
        targets.add(self.root,'human-alias',human)
        write(operations.play_record(self.root),{'active':True,'target':'human-alias','target_config':human})
        self.assertCode('human_session_active',self.execute)
        self.assertEqual(self.calls,[])

    def test_unrelated_device_does_not_block_isolated_verification(self):
        other=dict(self.target,device='abcdef9999')
        write(operations.play_record(self.root),{'active':True,'target':'other-phone','target_config':other})
        self.assertEqual(len(self.execute()[0]),2)

    def test_receipt_checks_source_runtime_and_target_before_dispatch(self):
        native={'exit_code':0,'data':{'ok':True,'revision':'authored'}}
        with patch.object(workflow,'native',return_value=native),patch.object(workflow,'Platform',self.fake_platform):
            write(self.deployment_path,dict(self.deployment,target_sha256='old'))
            self.assertCode('target_changed',lambda:workflow.receipt(self.root,'verify','snapshot',1,'quick',None,30,False,self.engine))
            write(self.deployment_path,dict(self.deployment,source_revision='old'))
            self.assertCode('deployment_stale',lambda:workflow.receipt(self.root,'verify','snapshot',1,'quick',None,30,False,self.engine))
            write(self.deployment_path,dict(self.deployment,engine_sha256='old'))
            self.assertCode('runtime_changed',lambda:workflow.receipt(self.root,'verify','snapshot',1,'quick',None,30,False,self.engine))
        self.assertFalse(any(call[0]=='control' for call in self.calls))

    def test_step_receipt_reads_one_project_local_input_without_changing_source(self):
        source_hash=sha(self.root/'project.json')
        input_path=self.root/'.langmake/control-input.json';value={'move_x':1,'actions':{'lamp':1}};write(input_path,value)
        native={'data':{'revision':'authored'}}
        with patch.object(workflow,'native',return_value=native),patch.object(workflow,'Platform',self.fake_platform),patch.object(self.fake_platform,'control',return_value={'ok':True,'result':{'action':'step','tick':60}}) as control:
            result=workflow.receipt(self.root,'verify','step',60,'quick',str(input_path),30,False,self.engine)
            self.assertTrue(result['ok']);self.assertEqual(control.call_args.kwargs['input'],value)
            self.assertEqual(control.call_args.kwargs['steps'],60)
            write(input_path,[])
            self.assertCode('invalid_request',lambda:workflow.receipt(self.root,'verify','step',1,'quick',str(input_path),30,False,self.engine))
            self.assertCode('path_outside_workspace',lambda:workflow.receipt(self.root,'verify','step',1,'quick',str(self.root.parent/'outside.json'),30,False,self.engine))
            self.assertEqual(control.call_count,1)
        self.assertEqual(sha(self.root/'project.json'),source_hash)

    def test_session_protect_prevents_mutations_until_released(self):
        protected=main(['session','protect','--project',str(self.root),'--target','verify']);self.assertTrue(protected['ok'])
        self.assertCode('human_session_active',lambda:operations.protect_play(self.root,'verify'))
        self.assertTrue(main(['session','release','--project',str(self.root)])['ok'])
        operations.protect_play(self.root,'verify')

    def test_context_and_handoff_resolve_profile_metadata_without_host_leak(self):
        calls=[]
        def native(engine,args,*rest,**kwargs):
            calls.append(args)
            if args[0]=='capabilities':return {'data':{'ok':True,'version':'0.5.0'}}
            return {'data':{'ok':True,'revision':'authored','profile':'phone','application':{'id':'org.test.profile','label':'Phone game'}}}
        with patch.object(workflow,'native',side_effect=native),patch.object(workflow,'prerequisite',return_value={'ok':True}):
            context=workflow.context(self.root,self.engine,'verify')
            self.assertIn('--profile',next(a for a in calls if a[0]=='inspect'))
            self.assertEqual(context['project']['resolved_profile'],'phone')
            self.assertTrue(workflow.handoff(self.root,self.engine)['ok'])
        handoff=read(self.root/'HANDOFF.json')
        self.assertEqual(handoff['application']['id'],'org.test.profile')
        self.assertEqual(handoff['resolved_profile'],'phone')
        portable=json.dumps(handoff)
        for private in ['explicit-mac','/selected/workspace','abcdef1234',str(self.root/'ssh-config')]:self.assertNotIn(private,portable)

    def native_run(self,engine,args,folder,label,**kwargs):
        self.calls.append(('native',list(args)))
        data={'ok':True}
        if args[0]=='validate':data['revision']='authored'
        if args[0]=='ui-check':
            data['diagnostics']=[];write(Path(args[args.index('--output')+1]),data)
        if args[0]=='simulate':
            tick=int(args[args.index('--steps')+1])
            state={'ok':True,'tick':tick,'state':{'won':True}}
            write(Path(args[args.index('--output')+1]),{'state':state})
        return {'exit_code':0,'data':data}

    def test_complete_run_replays_actual_device_viewport_and_profile(self):
        self.sidecar_updates={'state':{'won':True}}
        write(self.root/'smoke.json',{'steps':12,'capture_ticks':[0,12],'assertions':[{'path':'/state/won','equals':True}]})
        with patch.object(verify,'native',side_effect=self.native_run):
            result=verify.run(self.root,self.engine,'smoke',target_name='verify')
        self.assertTrue(result['ok'],result)
        self.assertEqual(result['evidence'],['host_tested','physically_rendered'])
        self.assertFalse(result['human_playtested']);self.assertFalse(result['physically_input_tested'])
        device_references=[c[1] for c in self.calls if c[0]=='native' and '--output' in c[1] and 'device-reference-' in str(c[1][c[1].index('--output')+1])]
        self.assertEqual(len(device_references),2)
        for args in device_references:
            self.assertEqual(args[args.index('--profile')+1],'phone')
            self.assertEqual(args[args.index('--width')+1],'800')
            self.assertEqual(args[args.index('--density')+1],'2')
            self.assertEqual(args[args.index('--safe-area')+1],'10,10,780,580')

    def test_native_assertions_cannot_be_satisfied_only_by_host(self):
        self.sidecar_updates={'state':{'won':True}};self.final_updates={'state':{'won':False}}
        write(self.root/'smoke.json',{'steps':12,'capture_ticks':[0,12],'assertions':[{'path':'/state/won','equals':True}]})
        with patch.object(verify,'native',side_effect=self.native_run):result=verify.run(self.root,self.engine,'smoke',target_name='verify')
        self.assertFalse(result['ok']);self.assertEqual(result['failure']['code'],'assertion_failed')
        report=read(result['report']);self.assertTrue(report['assertions'][0]['ok']);self.assertFalse(report['native_assertions'][0]['ok'])

    def test_profile_mismatch_fails_before_contacting_device(self):
        with patch.object(verify,'native') as native:
            result=verify.run(self.root,self.engine,'smoke',profile='desktop',target_name='verify')
        self.assertEqual(result['failure']['code'],'profile_mismatch');native.assert_not_called();self.assertEqual(self.calls,[])

    def test_android_physical_label_requires_reported_device_property(self):
        self.target={'platform':'android','serial':'explicit-phone','abi':'arm64-v8a','role':'verification','profile':'phone','application_id':'org.test.human','verification_application_id':'org.test.verify'}
        targets.remove(self.root,'verify');targets.add(self.root,'verify',self.target)
        self.deployment.update(target_config=self.target,target_sha256=targets.fingerprint(self.target));write(self.deployment_path,self.deployment)
        with patch.object(self.fake_platform,'call',return_value={'data':{'devices':[{'serial':'explicit-phone','is_emulator':None}]}}):
            self.assertEqual(self.execute()[1],'simulator_or_unclassified_rendered')
        flags=next(c[2] for c in self.calls if c[0]=='launch' and '--steps' in c[2])
        self.assertEqual(flags[flags.index('--input')+1],'scenario-'+self.folder.name+'.json')
        self.assertEqual(flags[flags.index('--output-dir')+1],'evidence/scenario-'+self.folder.name)
        self.assertFalse(flags[flags.index('--input')+1].startswith('/'))

    def test_session_launch_defaults_to_guarded_player_without_build_or_install(self):
        live={'session':'new-player','source_revision':'packaged','tick':3,'mode':'play'}
        with patch.object(workflow,'native',return_value={'data':{'revision':'authored'}}),patch.object(workflow,'Platform',self.fake_platform),patch.object(self.fake_platform,'fresh_live',return_value=live) as observe:
            result=workflow.launch_session(self.root,self.engine,'verify')
        self.assertTrue(result['ok'],result)
        self.assertEqual(observe.call_args.kwargs['previous'],'before-bootstrap')
        self.assertEqual(observe.call_args.kwargs['revision'],'packaged')
        self.assertTrue(read(operations.play_record(self.root))['active'])
        self.assertEqual(next(c for c in self.calls if c[0]=='launch')[2],[])
        self.assertEqual(result['mode'],'play')

    def test_cli_session_development_launch_uses_explicit_flags_without_human_guard(self):
        live={'session':'new-development','source_revision':'packaged','tick':0,'mode':'preview'}
        with patch.object(workflow,'native',return_value={'data':{'revision':'authored'}}),patch.object(workflow,'Platform',self.fake_platform),patch.object(self.fake_platform,'fresh_live',return_value=live):
            result=main(['--engine',str(self.engine),'session','launch','--project',str(self.root),'--target','verify','--development','--fresh'])
        self.assertTrue(result['ok'],result)
        self.assertEqual(next(c for c in self.calls if c[0]=='launch')[2],['--development','--fresh'])
        self.assertFalse(operations.play_record(self.root).exists())

    def test_session_launch_preserves_human_until_explicit_end(self):
        write(operations.play_record(self.root),{'active':True,'target':'verify','target_config':self.target})
        live={'session':'new-player','source_revision':'packaged','mode':'play'}
        with patch.object(workflow,'native',return_value={'data':{'revision':'authored'}}),patch.object(workflow,'Platform',self.fake_platform),patch.object(self.fake_platform,'fresh_live',return_value=live):
            self.assertCode('human_session_active',lambda:workflow.launch_session(self.root,self.engine,'verify'))
            self.assertFalse(any(c[0]=='launch' for c in self.calls))
            self.assertTrue(workflow.launch_session(self.root,self.engine,'verify',end_play=True)['ok'])

    def test_session_launch_uncertainty_is_recorded_without_replay(self):
        with patch.object(workflow,'native',return_value={'data':{'revision':'authored'}}),patch.object(workflow,'Platform',self.fake_platform),patch.object(self.fake_platform,'launch',side_effect=TimeoutError('lost acknowledgment')) as launch:
            result=workflow.launch_session(self.root,self.engine,'verify')
        self.assertEqual(result['status'],'uncertain');self.assertEqual(result['failure']['code'],'launch_uncertain')
        self.assertEqual(launch.call_count,1);self.assertEqual(read(result['record'])['status'],'uncertain')

    def test_session_launch_observation_failure_keeps_human_guard(self):
        with patch.object(workflow,'native',return_value={'data':{'revision':'authored'}}),patch.object(workflow,'Platform',self.fake_platform),patch.object(self.fake_platform,'fresh_live',side_effect=WorkflowError('observation_timeout','no new session')):
            result=workflow.launch_session(self.root,self.engine,'verify')
        self.assertFalse(result['ok']);self.assertEqual(result['failure']['code'],'observation_timeout')
        self.assertTrue(read(operations.play_record(self.root))['active'])
        self.assertEqual(sum(c[0]=='launch' for c in self.calls),1)

    def test_session_launch_rejects_stale_provenance_before_dispatch(self):
        with patch.object(workflow,'native',return_value={'data':{'revision':'authored'}}),patch.object(workflow,'Platform') as adapter:
            for key,value,code in [('source_revision','old','deployment_stale'),('engine_sha256','old','runtime_changed'),('target_sha256','old','target_changed')]:
                write(self.deployment_path,dict(self.deployment,**{key:value}))
                self.assertCode(code,lambda:workflow.launch_session(self.root,self.engine,'verify'))
            adapter.assert_not_called()

    def test_session_launch_human_flags_and_windows_are_rejected(self):
        with patch.object(workflow.targets,'selected',return_value=dict(self.target,role='human')):
            self.assertCode('verification_target_required',lambda:workflow.launch_session(self.root,self.engine,'verify',development=True))
            self.assertCode('verification_target_required',lambda:workflow.launch_session(self.root,self.engine,'verify',fresh=True))
        with patch.object(workflow.targets,'selected',return_value={'platform':'windows'}):
            self.assertCode('invalid_target',lambda:workflow.launch_session(self.root,self.engine,'verify'))

    def test_session_launch_release_reports_unavailable_observation(self):
        self.target=dict(self.target,configuration='release');targets.remove(self.root,'verify');targets.add(self.root,'verify',self.target)
        self.deployment.update(target_config=self.target,target_sha256=targets.fingerprint(self.target));write(self.deployment_path,self.deployment)
        with patch.object(workflow,'native',return_value={'data':{'revision':'authored'}}),patch.object(workflow,'Platform',self.fake_platform),patch.object(self.fake_platform,'fresh_live') as observe:
            result=workflow.launch_session(self.root,self.engine,'verify')
        self.assertTrue(result['ok']);self.assertFalse(result['observation']['verified_live']);observe.assert_not_called()


if __name__=='__main__':unittest.main()
