"""Declarative, bounded technical scenarios. Never a claim of human acceptance."""
from __future__ import annotations
import math
import re
from pathlib import Path, PurePosixPath
import time
from .common import WorkflowError, failure, local, native, now, pointer, profile_arguments, read, record, sha, within, write
from .targets import selected, fingerprint
from .transport import Platform
from .operations import protect_play

STATE_FIELDS = ('tick','scene','dimension','state','entities','camera','ui','won','collected','grounded','navigation_agents')


def scenario(project, name):
    registry = read(project/'project.json').get('scenarios', {})
    if not isinstance(registry,dict) or name not in registry:
        raise WorkflowError('scenario_missing', 'Register this named scenario in project.json scenarios.', scenario=name)
    if not isinstance(registry[name],str):raise WorkflowError('invalid_scenario','Registered scenarios require project-relative file paths.')
    value = read(within(project/registry[name], project))
    if not isinstance(value,dict) or set(value)-{'steps','input','capture_ticks','assertions','viewport','tolerances','save_roundtrip'}:
        raise WorkflowError('invalid_scenario','Scenario contains unknown fields or is not an object.')
    steps=value.get('steps',120)
    if type(steps) is not int or not 0<=steps<=36000:raise WorkflowError('invalid_scenario','steps must be an integer within 0..36000.')
    ticks=value.get('capture_ticks',sorted(set([0,steps])))
    if type(steps) is not int or not 0<=steps<=36000 or not isinstance(ticks,list) or not 1<=len(ticks)<=128 or any(type(t) is not int or not 0<=t<=steps for t in ticks) or sorted(set(ticks))!=ticks:
        raise WorkflowError('invalid_scenario','steps must be 0..36000; capture_ticks must be 1..128 increasing unique ticks within steps.')
    timeline=value.get('input',{})
    if isinstance(timeline,str):timeline=read(within(project/timeline,project))
    if not isinstance(timeline,dict):raise WorkflowError('invalid_scenario','input must be a timeline object or registered project-relative JSON path.')
    assertions=value.get('assertions',[])
    if not isinstance(assertions,list) or len(assertions)>256:raise WorkflowError('invalid_scenario','At most 256 assertions are supported.')
    for item in assertions:
        if not isinstance(item,dict) or set(item)-{'path','equals','min','max','exists'} or not isinstance(item.get('path'),str) or not item['path'].startswith('/') or re.search(r'~(?![01])',item['path']) or len(item)==1:
            raise WorkflowError('invalid_assertion','Use path plus equals, min, max or exists.')
        for key in ('min','max'):
            if key in item and (type(item[key]) not in (int,float) or not math.isfinite(item[key])):raise WorkflowError('invalid_assertion','Numeric assertion bounds must be finite.')
        if 'exists' in item and type(item['exists']) is not bool:raise WorkflowError('invalid_assertion','exists must be boolean.')
    tolerance=value.get('tolerances',{})
    if not isinstance(tolerance,dict) or set(tolerance)-{'absolute','relative'}:raise WorkflowError('invalid_scenario','Unknown tolerance fields.')
    for key,default in [('absolute',1e-4),('relative',1e-6)]:
        item=tolerance.setdefault(key,default)
        if type(item) not in (int,float) or not math.isfinite(item) or not 0<=item<=.1:raise WorkflowError('invalid_scenario','Comparison tolerances must be finite and within 0..0.1.')
    viewport=value.get('viewport',{})
    if not isinstance(viewport,dict) or set(viewport)-{'width','height','density','safe_area'}:raise WorkflowError('invalid_scenario','Unknown viewport fields.')
    for key,default in [('width',1280),('height',800),('density',1)]:viewport.setdefault(key,default)
    viewport_args(viewport)
    if type(value.get('save_roundtrip',False)) is not bool:raise WorkflowError('invalid_scenario','save_roundtrip must be boolean.')
    return dict(value,steps=steps,capture_ticks=ticks,input=timeline,assertions=assertions,tolerances=tolerance,viewport=viewport)


def assertions(snapshot, checks):
    results=[]
    for item in checks:
        try:actual=pointer(snapshot,item['path']);exists=True
        except (KeyError,IndexError,ValueError,TypeError):actual=None;exists=False
        ok=True
        for key,expected in item.items():
            if key=='path':continue
            if key=='exists':passed=exists==expected
            elif not exists:passed=False
            elif key=='equals':passed=actual==expected and (type(actual) is bool)==(type(expected) is bool)
            else:passed=type(actual) in (int,float) and (actual>=expected if key=='min' else actual<=expected)
            ok=ok and passed
        results.append({'ok':ok,'assertion':item,'actual':actual,'exists':exists})
    return results


def differences(expected, actual, absolute=1e-4, relative=1e-6, path='', limit=64):
    result=[]
    def compare(a,b,p):
        if len(result)>=limit:return
        if type(a) in (int,float) and type(b) in (int,float):
            if not math.isfinite(a) or not math.isfinite(b) or not math.isclose(a,b,abs_tol=absolute,rel_tol=relative):result.append({'path':p,'expected':a,'actual':b})
        elif isinstance(a,dict) and isinstance(b,dict):
            if set(a)!=set(b):result.append({'path':p,'missing':sorted(set(a)-set(b)),'extra':sorted(set(b)-set(a))})
            for k in sorted(set(a)&set(b)):compare(a[k],b[k],p+'/'+str(k).replace('~','~0').replace('/','~1'))
        elif isinstance(a,list) and isinstance(b,list):
            if len(a)!=len(b):result.append({'path':p,'expected_length':len(a),'actual_length':len(b)})
            for i,(x,y) in enumerate(zip(a,b)):compare(x,y,p+'/'+str(i))
        elif type(a)!=type(b) or a!=b:result.append({'path':p,'expected':a,'actual':b})
    compare(expected,actual,path)
    return result


def gameplay(value):return {key:value[key] for key in STATE_FIELDS if key in value}


def viewport_args(value):
    args=[]
    for field in ('width','height'):
        if field in value and (type(value[field]) is not int or not 1<=value[field]<=8192):raise WorkflowError('invalid_scenario','Viewport dimensions must be integers within 1..8192.')
    if 'density' in value and (type(value['density']) not in (int,float) or not math.isfinite(value['density']) or not .25<=value['density']<=8):raise WorkflowError('invalid_scenario','Viewport density must be finite within 0.25..8.')
    for field in ('width','height','density'):
        if field in value:args += ['--'+field,str(value[field])]
    if 'safe_area' in value:
        area=value['safe_area']
        if not isinstance(area,list) or len(area)!=4 or any(type(n) is not int for n in area):raise WorkflowError('invalid_scenario','safe_area uses four physical integer values [x,y,width,height].')
        if any(v<0 for v in area[:2]) or any(v<=0 for v in area[2:]) or ('width' in value and area[0]+area[2]>value['width']) or ('height' in value and area[1]+area[3]>value['height']):raise WorkflowError('invalid_scenario','safe_area must be a positive rectangle within the physical viewport.')
        args+=['--safe-area',','.join(map(str,area))]
    return args


def deployment_record(project,target_name,target,source_revision=None,engine_sha256=None):
    deployment=read(within(local(project,'deployments')/(target_name+'.json'),project))
    if not isinstance(deployment,dict):raise WorkflowError('invalid_deployment_record','Deployment record must be an object.')
    if deployment.get('target_sha256')!=fingerprint(target) or deployment.get('target_config')!=target:raise WorkflowError('target_changed','Deploy the explicitly configured target again after changing its settings.')
    if source_revision and deployment.get('source_revision')!=source_revision:raise WorkflowError('deployment_stale','Deploy the current selected source profile before verification or control.')
    if engine_sha256 and deployment.get('engine_sha256')!=engine_sha256:raise WorkflowError('runtime_changed','Deploy the current engine before verification or control.')
    for key in ('application_id','storage_id','packaged_revision'):
        if not isinstance(deployment.get(key),str) or not deployment[key]:raise WorkflowError('invalid_deployment_record','Deployment record lacks '+key+'.')
    return deployment


def mobile_captures(project,target_name,folder,spec,timeline,source_revision=None,engine_sha256=None):
    target=selected(project,target_name)
    if target['platform']=='windows':raise WorkflowError('invalid_target','Use --graphics for Windows native captures.')
    if target.get('role')!='verification' or not target.get('verification_application_id') or target.get('verification_application_id')==target.get('application_id'):
        raise WorkflowError('verification_identity_required','Native scenarios require a separately configured role=verification target and distinct verification_application_id.')
    if target.get('configuration','debug')!='debug':raise WorkflowError('debug_required','Native transport verification requires a debug app.')
    deployment=deployment_record(project,target_name,target,source_revision,engine_sha256)
    bundle=target['verification_application_id'];storage=deployment['storage_id']
    if deployment['application_id']!=bundle:raise WorkflowError('verification_identity_mismatch','Deploy this verification target before running its scenario.')
    protect_play(project,target_name)
    adapter=Platform(target,folder);adapter.prepare()
    observation=adapter.inspect(bundle,storage);data=observation.get('data');old=data.get('live',{}) if isinstance(data,dict) else {}
    if not isinstance(old,dict):old={}
    # First start the test app to establish its private storage. This is the
    # verification identity only; no human app is launched or reset.
    adapter.launch(bundle,['--fresh','--development','--no-audio'])
    live=adapter.fresh_live(bundle,storage,previous=old.get('session'),revision=deployment['packaged_revision'])
    native_root=live.get('storage_directory')
    if not isinstance(native_root,str) or not native_root.startswith('/') or len(native_root)>4096 or any(ord(c)<32 for c in native_root) or '\\' in native_root or '..' in PurePosixPath(native_root).parts:
        raise WorkflowError('storage_missing','Native observation did not expose private storage.')
    input_name='scenario-'+folder.name+'.json';output_name='evidence/scenario-'+folder.name
    adapter.push(bundle,storage,input_name,timeline)
    # Both launch contracts accept storage-relative paths. Android's Java
    # bootstrap requires them and resolves them before calling native code;
    # iOS resolves them inside its own application storage directory.
    flags=['--fresh','--steps',str(spec['steps']),'--input',input_name,'--ticks',','.join(map(str,spec['capture_ticks'])),'--output-dir',output_name,'--no-audio']
    adapter.launch(bundle,flags)
    deadline=time.monotonic()+min(900,max(60,spec['steps']/30+30));result_file=folder/'native-result.json'
    result=None;poll=0
    while time.monotonic()<deadline:
        poll+=1;result_file=folder/('native-result-'+str(poll)+'.json')
        pulled=adapter.pull(bundle,storage,'launch-result.json',result_file,missing_ok=True)
        if pulled:
            response=read(pulled)
            if not isinstance(response,dict):raise WorkflowError('malformed_result','Native scenario result must be an object.',result=str(result_file))
            if response.get('ok') is False:raise WorkflowError('native_scenario_failed',response.get('diagnostic','Native scenario failed.'),result=str(result_file))
            candidate=response.get('state')
            completed=response.get('captures')
            expected={output_name+'/tick-'+str(t)+'.png' for t in spec['capture_ticks']}
            # Completed paths contain our unique run ID. A different session
            # alone is insufficient: the prior scenario may also have finished.
            belongs=isinstance(completed,list) and len(completed)==len(expected) and all(isinstance(p,str) for p in completed) and {item for item in expected if any(p.endswith('/'+item) for p in completed)}==expected
            if belongs:
                if response.get('ok') is not True or not isinstance(candidate,dict) or not isinstance(candidate.get('session'),str) or not candidate['session'] or candidate['session']==live['session'] or candidate.get('running') is not False:
                    raise WorkflowError('scenario_mismatch','Completed evidence has an invalid scenario session.',result=str(result_file))
                result=candidate;break
        time.sleep(.5)
    if not result:raise WorkflowError('scenario_timeout','No completed result from the new scenario session; launch was not replayed.',result=str(result_file))
    if result.get('ok') is not True or result.get('source_revision')!=deployment['packaged_revision'] or type(result.get('tick')) is not int or result['tick']!=spec['steps']:
        raise WorkflowError('scenario_mismatch','Final native result has the wrong tick/revision.',result=str(result_file))
    captures=[]
    write(folder/'native-final-state.json',result)
    for tick in spec['capture_ticks']:
        image=folder/('tick-'+str(tick)+'.png');side=image.with_suffix('.png.json')
        adapter.pull(bundle,storage,output_name+'/'+image.name,image)
        adapter.pull(bundle,storage,output_name+'/'+side.name,side)
        value=read(side)
        if not isinstance(value,dict) or value.get('session')!=result['session'] or value.get('source_revision')!=deployment['packaged_revision'] or type(value.get('tick')) is not int or value['tick']!=tick:
            raise WorkflowError('capture_mismatch','Capture differs from final scenario identity.',sidecar=str(side))
        captures.append({'tick':tick,'image':str(image),'sidecar':str(side),'image_sha256':sha(image)})
    # Platform tools classify simulator/device; Android serial naming alone is
    # never evidence of physical hardware.
    physical=target['platform']=='ios' and target['destination']=='device'
    if target['platform']=='android':
        devices=adapter.call(['devices'])['data']
        entries=devices.get('devices',[]) if isinstance(devices,dict) else []
        device=next((d for d in entries if isinstance(d,dict) and d.get('serial')==target['serial']),{})
        physical=device.get('is_emulator') is False or device.get('type')=='physical'
    return captures,('physically_rendered' if physical else 'simulator_or_unclassified_rendered')


def run(project,engine,name,profile=None,graphics=False,target_name=None):
    identity,folder=record(project,'verification')
    report={'ok':False,'id':identity,'scenario':name,'created_at_ms':now(),'project':str(project),'profile':profile,
            'evidence':[],'human_playtested':False,'physically_input_tested':False,'checks':[]}
    try:
        if graphics and target_name:raise WorkflowError('invalid_verification','Use separate runs for Windows graphics and mobile target captures.')
        spec=scenario(project,name);write(folder/'scenario.json',spec)
        if target_name:
            configured=selected(project,target_name)
            if profile and profile!=configured.get('profile'):raise WorkflowError('profile_mismatch','Use the configured target profile for device comparisons.')
            profile=configured.get('profile');report['profile']=profile
            protect_play(project,target_name)
        profile_flags=profile_arguments(profile)
        source_revision=None
        for cmd in ('validate','check'):
            checked=native(engine,[cmd,'--project',project,*profile_flags],folder,cmd)['data']
            if cmd=='validate':source_revision=checked['revision']
            report['checks'].append({'name':cmd,'ok':True})
        layout=native(engine,['ui-check','--project',project,*profile_flags,*viewport_args(spec['viewport']),'--output',folder/'layout.json'],folder,'ui-check')['data']
        report['layout']={'diagnostics':layout.get('diagnostics',[]),'record':str(folder/'layout.json')}
        timeline=folder/'input.json';write(timeline,spec['input'])
        references={}
        for tick in sorted(set([spec['steps'],*spec['capture_ticks']])):
            output=folder/('host-'+str(tick)+'.json')
            args=['simulate','--project',project,*profile_flags,'--steps',str(tick),'--input',timeline,'--output',output,'--summary',*viewport_args(spec['viewport'])]
            if tick==spec['steps'] and spec.get('save_roundtrip'):args+=['--save-output',folder/'save.json']
            native(engine,args,folder,'host-'+str(tick));references[tick]=read(output)['state']
        report['assertions']=assertions(references[spec['steps']],spec['assertions'])
        if not all(x['ok'] for x in report['assertions']):raise WorkflowError('assertion_failed','Scenario assertions failed; inspect report.json.')
        report['evidence'].append('host_tested')
        if spec.get('save_roundtrip'):
            native(engine,['save-check','--project',project,*profile_flags,'--file',folder/'save.json'],folder,'save-check')
            for attempt in (1,2):
                output=folder/('restore-'+str(attempt)+'.json')
                native(engine,['simulate','--project',project,*profile_flags,'--steps','0','--load-save',folder/'save.json','--output',output,'--summary',*viewport_args(spec['viewport'])],folder,'restore-'+str(attempt))
                restored=read(output)['state']
                diff=differences(gameplay(references[spec['steps']]),gameplay(restored),**spec['tolerances'])
                if diff:raise WorkflowError('save_roundtrip_failed','Fresh-process restore changed observable game state.',differences=diff,attempt=attempt)
            report['checks'].append({'name':'fresh_process_save_restore_twice','ok':True})
        captures=[]
        if graphics:
            native(engine,['capture','--project',project,*profile_flags,'--steps',str(spec['steps']),'--input',timeline,'--ticks',','.join(map(str,spec['capture_ticks'])),'--output-dir',folder/'captures','--no-overlay',*viewport_args(spec['viewport'])],folder,'capture',timeout=900)
            captures=[{'tick':t,'image':str(folder/'captures'/('tick-'+str(t)+'.png')),'sidecar':str(folder/'captures'/('tick-'+str(t)+'.png.json'))} for t in spec['capture_ticks']]
            report['evidence'].append('windows_rendered')
        if target_name:
            captures,kind=mobile_captures(project,target_name,folder,spec,timeline,source_revision,sha(engine));report['evidence'].append(kind)
            report['native_assertions']=assertions(read(folder/'native-final-state.json'),spec['assertions'])
            if not all(item['ok'] for item in report['native_assertions']):raise WorkflowError('assertion_failed','Native final state failed scenario assertions.')
            # Device density/cutouts are authoritative. Reproduce the actual
            # observed viewport when a game explicitly queries presentation.
            for capture in captures:
                observed=read(capture['sidecar']);ui=observed.get('ui_renderer',{})
                actual={'width':observed['width'],'height':observed['height'],'density':ui.get('density',1),'safe_area':ui.get('safe_area',[0,0,observed['width'],observed['height']])}
                output=folder/('device-reference-'+str(capture['tick'])+'.json')
                native(engine,['simulate','--project',project,*profile_flags,'--steps',str(capture['tick']),'--input',timeline,'--output',output,'--summary',*viewport_args(actual)],folder,'device-reference-'+str(capture['tick']))
                references[capture['tick']]=read(output)['state']
        for capture in captures:
            value=read(capture['sidecar']);diff=differences(gameplay(references[capture['tick']]),gameplay(value),**spec['tolerances'])
            if diff:raise WorkflowError('capture_state_mismatch','Graphical state differs from host simulation.',tick=capture['tick'],differences=diff)
        report['captures']=captures;report['ok']=True
    except Exception as error:report['failure']=failure(error)
    report['finished_at_ms']=now();write(folder/'report.json',report)
    return {'ok':report['ok'],'run_id':identity,'scenario':name,'evidence':report['evidence'],'report':str(folder/'report.json'),
            'captures':report.get('captures',[]),'failure':report.get('failure'),'human_playtested':False,'physically_input_tested':False}
