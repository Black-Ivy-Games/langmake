#!/usr/bin/env python3
"""Optional authoring facade; exported games never run this Python module."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import sys
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from scripts.workflow.common import ROOT, WorkflowError, command, failure, local, native, now, project_root, profile_arguments, read, record, runtime, sha, within, write
from scripts.workflow import operations, targets, verify
from scripts.workflow.transport import Platform


def parser():
    result=argparse.ArgumentParser(description=__doc__)
    result.add_argument('--engine',help='Exact native engine executable (native facade fills this).')
    commands=result.add_subparsers(dest='command',required=True)
    for name in ('context','target','deploy','operation','verify','receipt','handoff','session'):
        p=commands.add_parser(name);p.add_argument('--project',required=True);p.add_argument('--json',action='store_true',help='Output is always JSON; accepted for explicit scripts.')
        if name=='context':p.add_argument('--target',help='Explicitly probe this configured target using its platform doctor.')
        elif name=='target':
            p.add_argument('action',choices=['list','add','check','remove']);p.add_argument('--name');p.add_argument('--file')
        elif name=='deploy':
            p.add_argument('--target',required=True);p.add_argument('--stop-after',choices=['build','install','launch'],default='launch');p.add_argument('--end-play-session',action='store_true')
        elif name=='operation':
            p.add_argument('action',choices=['show','resume']);p.add_argument('--id',required=True);p.add_argument('--full',action='store_true');p.add_argument('--end-play-session',action='store_true')
        elif name=='verify':
            p.add_argument('--scenario',required=True);p.add_argument('--profile');p.add_argument('--graphics',action='store_true');p.add_argument('--target')
        elif name=='receipt':
            p.add_argument('--target',required=True);p.add_argument('--action',choices=['snapshot','capture','pause','resume','step','reset','save','load'],required=True)
            p.add_argument('--steps',type=int,default=1);p.add_argument('--input');p.add_argument('--slot',default='quick');p.add_argument('--timeout',type=int,default=30);p.add_argument('--end-play-session',action='store_true')
        elif name=='handoff':
            p.add_argument('--brief',help='Project-relative text/Markdown file with human brief/accepted feedback.')
            p.add_argument('--notes',help='Project-relative text/Markdown file with known issues/pending human actions.')
        elif name=='session':
            p.add_argument('action',choices=['show','release','protect','launch']);p.add_argument('--target')
            p.add_argument('--development',action='store_true');p.add_argument('--fresh',action='store_true');p.add_argument('--end-play-session',action='store_true')
    return result


def prerequisite(project,engine,name):
    target=targets.selected(project,name)
    _,folder=record(project,'checks')
    if target['platform']=='windows':
        info=native(engine,['capabilities'],folder,'capabilities')['data']
        return {'ok':True,'target':name,'platform':'windows','capabilities':info,'evidence':str(folder)}
    adapter=Platform(target,folder)
    probe=adapter.call(['doctor'],timeout=90,check=False)
    data=probe.get('data')
    if adapter.remote and not isinstance(data,dict):
        error=Path(probe['stderr']).read_text(encoding='utf-8',errors='replace')[:8192]
        code='ssh_authentication_rejected' if 'Permission denied' in error else 'ssh_connection_failed'
        raise WorkflowError(code,'The explicitly selected per-game SSH configuration could not run the platform doctor; no other keys were tried.',stderr=probe['stderr'])
    return {'ok':probe['exit_code']==0 and isinstance(data,dict) and data.get('ok',True),'target':name,'prerequisites':data,'evidence':str(folder)}


def context(project,engine,target=None):
    _,folder=record(project,'checks')
    cap=native(engine,['capabilities'],folder,'capabilities')['data']
    selection=targets.selected(project,target) if target else None
    info=native(engine,['inspect','--project',project,'--limit','1',*profile_arguments(selection.get('profile') if selection else None)],folder,'project')['data']
    document=read(project/'project.json')
    try:
        configured=targets.load(project)
        configuration={'ok':True,'file':str(targets.config_path(project)),'targets':configured['targets']}
    except WorkflowError as error:configuration=failure(error)
    result={'ok':True,'protocol_version':1,'workspace':str(ROOT),'runtime':{'path':str(engine),'sha256':sha(engine),'version':cap.get('version'),
            'capabilities':cap},'tools':{'path':str(Path(__file__).resolve()),'sha256':sha(__file__)},
            'project':{'path':str(project),'id':document.get('project_id'),'title':document.get('title'),'revision':info.get('revision',info.get('source_revision')),
                       'profiles':list(document.get('profiles',{})),'default_profile':document.get('default_profile'),'resolved_profile':info.get('profile'),'scenarios':list(document.get('scenarios',{})),
                       'application':info.get('application',document.get('application',{}))},
            'local_configuration':configuration,
            'documentation':{name:str(ROOT/path) for name,path in {'workflow':'docs/26-game-workflow.md','agent_api':'docs/08-agent-api.md','runtime':'docs/13-runtime-guide.md','music_sound':'docs/30-music-and-sound-guide.md','ui_editor':'docs/34-ui-editor-guide.md','android':'docs/21-android-agent-guide.md','ios':'docs/23-ios-agent-guide.md','prompts':'prompts/README.md'}.items()}}
    if target:result['target_check']=prerequisite(project,engine,target)
    return result


def handoff(project,engine,brief=None,notes=None):
    manifest=read(project/'project.json')
    info=context(project,engine)
    references={}
    for key,value in [('brief',brief),('notes',notes)]:
        if value:
            file=within(project/value,project)
            if not file.is_file() or file.stat().st_size>1024*1024:raise WorkflowError('invalid_handoff','Handoff references require existing project files up to 1 MiB.')
            references[key]={'path':file.relative_to(project).as_posix(),'sha256':sha(file)}
    reports=[]
    for path in sorted(local(project,'verification').glob('*/report.json'),key=lambda p:p.stat().st_mtime,reverse=True)[:8]:
        value=read(path)
        reports.append({'run_id':value['id'],'scenario':value['scenario'],'ok':value['ok'],'evidence':value.get('evidence',[]),
                        'record':path.relative_to(project).as_posix(),'human_playtested':False,'physically_input_tested':False})
    exports=[]
    for path in sorted(local(project,'operations').glob('*/operation.json'),key=lambda p:p.stat().st_mtime,reverse=True)[:8]:
        value=read(path)
        exports.append({'operation_id':value['id'],'status':value['status'],'platform':value['target']['platform'],
                        'profile':value['target'].get('profile'),'application_id':value.get('application_id'),'packaged_revision':value.get('packaged_revision'),
                        'local_record':path.relative_to(project).as_posix()})
    result={'generated_by':'langmake handoff','format_version':1,'updated_at_ms':now(),'title':manifest.get('title'),'project_id':manifest.get('project_id'),
            'engine_version':info['runtime']['version'],'source_revision':info['project']['revision'],'references':references,
            'profiles':info['project']['profiles'],'default_profile':manifest.get('default_profile'),'resolved_profile':info['project'].get('resolved_profile'),
            'scenarios':info['project']['scenarios'],'application':info['project']['application'],
            'save_version':manifest.get('save_version',1),'save_compatibility':manifest.get('save_compatibility'),
            'local_setup':'.langmake/targets.local.json (ignored; configure independently for each game/machine)',
            'verification':reports,'exports':exports,'human_playtesting':'awaiting feedback; technical verification is not creative acceptance'}
    destination=within(project/'HANDOFF.json',project)
    if destination.exists():
        previous=read(destination)
        if not isinstance(previous,dict) or previous.get('generated_by')!='langmake handoff':raise WorkflowError('handoff_exists','HANDOFF.json is not a generated handoff; preserve it and choose a different name manually.')
    write(destination,result)
    return {'ok':True,'handoff':str(destination),'portable':True,'verification_runs':len(reports),'exports':len(exports),'human_playtesting':result['human_playtesting']}


def receipt(project,name,action,steps,slot,input_path,timeout,end_play,engine=None):
    if not 1<=timeout<=120 or not 1<=steps<=600:raise WorkflowError('invalid_request','timeout must be 1..120 seconds and steps 1..600.')
    if action not in ('snapshot','capture','save'):operations.protect_play(project,name,end_play)
    target=targets.selected(project,name)
    if target['platform']=='windows':raise WorkflowError('invalid_target','Desktop uses native control/save/load with its project path.')
    if target.get('configuration','debug')!='debug':raise WorkflowError('debug_required','Private request/receipt transport requires a debug app.')
    _,folder=record(project,'receipts')
    engine=runtime(engine)
    info=native(engine,['inspect','--project',project,'--limit','1',*profile_arguments(target.get('profile'))],folder,'source')['data']
    revision=info.get('revision',info.get('source_revision'))
    if not isinstance(revision,str) or not revision:raise WorkflowError('source_revision_missing','Native inspection must identify the selected source revision before control.')
    deployment=verify.deployment_record(project,name,target,revision,sha(engine))
    adapter=Platform(target,folder);adapter.prepare()
    live=adapter.fresh_live(deployment['application_id'],deployment['storage_id'],revision=deployment['packaged_revision'])
    fields={'steps':steps}
    if action in ('save','load'):targets.identifier(slot,'save slot');fields['slot']=slot
    if input_path:
        if action!='step':raise WorkflowError('invalid_request','--input applies only to a step request.')
        fields['input']=read(within(input_path,project))
        if not isinstance(fields['input'],dict):raise WorkflowError('invalid_request','Step input must be a JSON object.')
    result=adapter.control(deployment['application_id'],deployment['storage_id'],live,action,timeout=timeout,**fields)
    full=result.pop('result',{})
    result.update({k:full[k] for k in ('action','tick','session','source_revision','image','sidecar') if k in full})
    return result


def launch_session(project,engine,name,development=False,fresh=False,end_play=False):
    """Explicitly relaunch one verified installed mobile app; never retry launch."""
    target=targets.selected(project,name)
    if target['platform']=='windows':raise WorkflowError('invalid_target','Windows uses the exported Play.cmd launcher or native play --project PATH; session launch is for installed mobile apps.')
    if (development or fresh) and target.get('role','human')!='verification':raise WorkflowError('verification_target_required','--development and --fresh require a separately configured verification target.')
    if (development or fresh) and target.get('configuration','debug')!='debug':raise WorkflowError('debug_required','Development/fresh launch flags require a debug verification app.')
    with operations.deployment_lock(project):
        identity,folder=record(project,'sessions')
        info=native(engine,['inspect','--project',project,'--limit','1',*profile_arguments(target.get('profile'))],folder,'source')['data']
        revision=info.get('revision',info.get('source_revision'))
        if not isinstance(revision,str) or not revision:raise WorkflowError('source_revision_missing','Native inspection must identify the selected source revision before launching.')
        deployment=verify.deployment_record(project,name,target,revision,sha(engine))
        operations.protect_play(project,name,end_play)
        state={'format_version':1,'id':identity,'target':name,'target_sha256':targets.fingerprint(target),
               'application_id':deployment['application_id'],'storage_id':deployment['storage_id'],'source_revision':revision,
               'packaged_revision':deployment['packaged_revision'],'engine_sha256':sha(engine),'mode':'preview' if development else 'play',
               'fresh':fresh,'created_at_ms':now(),'status':'preparing'}
        destination=folder/'session.json'
        write(destination,state)
        try:
            adapter=Platform(target,folder);adapter.prepare()
            previous=None
            if target.get('configuration','debug')=='debug':
                response=adapter.inspect(deployment['application_id'],deployment['storage_id']);data=response.get('data')
                old=data.get('live',{}) if isinstance(data,dict) else {}
                previous=old.get('session') if isinstance(old,dict) and isinstance(old.get('session'),str) else None
            flags=(['--development'] if development else [])+(['--fresh'] if fresh else [])
            state.update(status='dispatching',previous_session=previous,flags=flags);write(destination,state)
            try:
                state['launch']=adapter.launch(deployment['application_id'],flags)
            except Exception as error:
                raise WorkflowError('launch_uncertain','The explicit launch lost its acknowledgment; inspect the recorded device outcome before requesting another launch. It was not retried.',record=str(destination),transport=failure(error)) from error
            state['status']='launched';write(destination,state)
            if not development:
                write(operations.play_record(project),{'active':True,'target':name,'target_config':target,
                      'session_launch_id':identity,'application_id':deployment['application_id'],'started_at_ms':now(),
                      'note':'Human player session; confirm completion before a disruptive update or relaunch.'})
            if target.get('configuration','debug')=='debug':
                live=adapter.fresh_live(deployment['application_id'],deployment['storage_id'],previous=previous,revision=deployment['packaged_revision'])
                if live.get('mode')!=state['mode']:raise WorkflowError('unexpected_mode','New native session did not enter the requested player/development mode.')
                write(folder/'live.json',live)
                state['observation']={'verified_live':True,'session':live['session'],'source_revision':live['source_revision'],'tick':live.get('tick'),'record':str(folder/'live.json')}
            else:
                state['observation']={'verified_live':False,'status':'unavailable_release_private_transport','note':'Launch acknowledged; release private inspection is unavailable. Human verification is required.'}
            state['status']='complete'
        except Exception as error:
            state['status']='uncertain' if state['status']=='dispatching' else 'blocked'
            state['failure']=failure(error)
        state['finished_at_ms']=now();write(destination,state)
        return {'ok':state['status']=='complete','run_id':identity,'status':state['status'],'target':name,
                'application_id':state['application_id'],'mode':state['mode'],'record':str(destination),
                'observation':state.get('observation'),'failure':state.get('failure')}


def main(argv=None):
    args=parser().parse_args(argv)
    try:
        project=project_root(args.project)
        if args.command=='target':
            if args.action=='list':return {'ok':True,'config':str(targets.config_path(project)),**targets.load(project)}
            if not args.name:raise WorkflowError('target_required','Select --name for this per-game target.')
            if args.action=='add':
                if not args.file:raise WorkflowError('target_file_required','Use --file with explicit settings for this game.')
                return targets.add(project,args.name,read(within(args.file)))
            if args.action=='remove':return targets.remove(project,args.name)
            return prerequisite(project,runtime(args.engine),args.name)
        if args.command=='session':
            if args.action=='launch':
                if not args.target:raise WorkflowError('target_required','session launch requires an explicitly configured --target.')
                return launch_session(project,runtime(args.engine),args.target,args.development,args.fresh,args.end_play_session)
            if args.development or args.fresh or args.end_play_session:raise WorkflowError('invalid_session_options','Launch flags apply only to session launch.')
            path=operations.play_record(project);value=read(path) if path.exists() else {'active':False}
            if not isinstance(value,dict):raise WorkflowError('invalid_session_record','Human session record must be an object.')
            if args.action=='protect':
                if not args.target:raise WorkflowError('target_required','session protect requires the target being handed to a human.')
                target=targets.selected(project,args.target)
                if value.get('active') and value.get('target')!=args.target:raise WorkflowError('human_session_active','A different target is already protected; confirm its human session is finished before releasing it.')
                value={'active':True,'target':args.target,'target_config':target,'started_at_ms':now(),'note':'Human playtesting session; confirm completion before release.'}
                write(path,value)
            if args.action=='release':value.update(active=False,released_at_ms=now());write(path,value)
            return {'ok':True,'session':value,'record':str(path)}
        if args.command=='operation':
            if args.action=='resume':return operations.resume(project,args.id,args.end_play_session)
            folder,value=operations.inspect_operation(project,args.id)
            return dict(ok=True,operation=value) if args.full else operations.summary(folder,value)
        engine=runtime(args.engine)
        if args.command=='context':return context(project,engine,args.target)
        if args.command=='deploy':return operations.initial(project,engine,args.target,args.stop_after,args.end_play_session)
        if args.command=='verify':
            if args.graphics and args.target:raise WorkflowError('invalid_verification','Use separate runs for Windows --graphics and a mobile --target.')
            return verify.run(project,engine,args.scenario,args.profile,args.graphics,args.target)
        if args.command=='receipt':return receipt(project,args.target,args.action,args.steps,args.slot,args.input,args.timeout,args.end_play_session,engine)
        if args.command=='handoff':return handoff(project,engine,args.brief,args.notes)
    except Exception as error:return failure(error)


if __name__=='__main__':
    result=main();print(json.dumps(result,indent=2,ensure_ascii=False));raise SystemExit(0 if result.get('ok') else 1)
