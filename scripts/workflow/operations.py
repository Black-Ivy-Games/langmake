"""Recorded deployment phases. Resumption never repeats a completed mutation."""
from __future__ import annotations
import json
import inspect
import uuid
from pathlib import Path
import shutil
import subprocess
import sys
import time
import zipfile
from contextlib import contextmanager
from .common import ROOT, WorkflowError, command, failure, local, native, now, profile_arguments, read, record, sha, within, write
from .targets import selected, fingerprint
from .transport import Platform, valid_live


def play_record(project):
    return within(project / '.langmake/human-session.json', project)


def protect_play(project, target_name, release=False, operation_id=None):
    path=play_record(project)
    value=read(path) if path.exists() else {}
    if not isinstance(value,dict): raise WorkflowError('invalid_session_record','Human session record must be an object.')
    if value.get('active') and (operation_id is None or value.get('operation_id')!=operation_id):
        # A target alias must not bypass protection of the same physical phone.
        target=selected(project,target_name)
        previous=value.get('target_config')
        same = value.get('target')==target_name or not isinstance(previous,dict) or endpoint(previous)==endpoint(target)
        if not same:return
        if not release:
            raise WorkflowError('human_session_active','This game was handed to a human. Confirm they have finished, then use --end-play-session or session release before a disruptive update.',session=str(path))
        value.update(active=False,released_at_ms=now())
        write(path,value)


def endpoint(target):
    host=target.get('host',{'transport':'local'})
    return (target['platform'],host.get('transport'),host.get('alias'),host.get('config'),target.get('device'),target.get('serial'))


@contextmanager
def deployment_lock(project):
    """OS-owned lock releases on process exit; stale files never block recovery."""
    path=within(project/'.langmake/deployment.lock',project);path.parent.mkdir(parents=True,exist_ok=True)
    stream=path.open('a+b');locked=False
    try:
        if stream.tell()==0:stream.write(b'0');stream.flush()
        stream.seek(0)
        try:
            if sys.platform=='win32':
                import msvcrt
                msvcrt.locking(stream.fileno(),msvcrt.LK_NBLCK,1)
            else:
                import fcntl
                fcntl.flock(stream.fileno(),fcntl.LOCK_EX|fcntl.LOCK_NB)
            locked=True
        except OSError as error:
            raise WorkflowError('deployment_busy','Another deployment is running for this game; wait for its recorded result.') from error
        yield
    finally:
        if locked:
            stream.seek(0)
            if sys.platform=='win32':
                import msvcrt
                msvcrt.locking(stream.fileno(),msvcrt.LK_UNLCK,1)
            else:
                import fcntl
                fcntl.flock(stream.fileno(),fcntl.LOCK_UN)
        stream.close()


def stage_zip(folder,output):
    with zipfile.ZipFile(output,'x',zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(folder.rglob('*')):
            if path.is_symlink():
                raise WorkflowError('linked_stage','Staged content must not contain symlinks.')
            if path.is_file():
                archive.write(path,path.relative_to(folder).as_posix())


def extract_stage_archive(archive, destination, root):
    """Validate the entire archive before creating files; reject alias paths."""
    import pathlib, zipfile, stat, unicodedata, shutil
    destination, root = pathlib.Path(destination), pathlib.Path(root).resolve()
    if not destination.resolve().is_relative_to(root) or destination.resolve() == root:
        raise ValueError('Stage extraction escapes the operation directory')
    if any(p.is_symlink() for p in [destination, *destination.parents] if p.is_relative_to(root)):
        raise ValueError('Stage extraction cannot traverse symlinks')
    with zipfile.ZipFile(archive) as z:
        items = z.infolist()
        if len(items) > 16384 or sum(i.file_size for i in items) > 268435456:
            raise ValueError('Stage archive exceeds limits')
        names, files = set(), set()
        for item in items:
            name = item.filename.rstrip('/') if item.is_dir() else item.filename
            parts = name.split('/')
            if not name or any(p in ('', '.', '..') for p in parts) or any(ord(c) < 32 for c in name) or ':' in name or '\\' in name:
                raise ValueError('Unsafe archive member path')
            normalized = unicodedata.normalize('NFC', name).casefold()
            mode = stat.S_IFMT(item.external_attr >> 16)
            if normalized in names or mode not in (0, stat.S_IFREG, stat.S_IFDIR) or (mode == stat.S_IFDIR and not item.is_dir()):
                raise ValueError('Duplicate, aliased or special archive member')
            names.add(normalized)
            if not item.is_dir(): files.add(normalized)
        if any('/'.join(name.split('/')[:i]) in files for name in names for i in range(1, len(name.split('/')))):
            raise ValueError('Archive file collides with a parent directory')
        destination.mkdir(parents=False, exist_ok=False)
        for item in items:
            output = destination / item.filename
            if item.is_dir(): output.mkdir(parents=True, exist_ok=True)
            else:
                output.parent.mkdir(parents=True, exist_ok=True)
                with z.open(item) as source, output.open('xb') as target:
                    shutil.copyfileobj(source, target, 1024 * 1024)
    return {'ok': True, 'stage': str(destination)}


def extract_remote(adapter, archive, destination):
    script = 'import sys,json\n' + inspect.getsource(extract_stage_archive) + '\nprint(json.dumps(extract_stage_archive(sys.argv[1],sys.argv[2],sys.argv[3])))'
    return command(adapter.ssh_arguments(adapter.remote_command([adapter.host.get('python','python3'),'-c',script,archive,destination,adapter.remote_folder])),
                   adapter.logs,adapter.label('extract'),timeout=60)['data']


def initial(project,engine,target_name,stop_after='launch',end_play=False):
    target=selected(project,target_name)
    if stop_after not in ('build','install','launch'): raise WorkflowError('invalid_phase','stop_after must be build, install or launch.')
    if stop_after != 'build': protect_play(project,target_name,end_play)
    identity,folder=record(project,'operations')
    info=native(engine,['inspect','--project',project,*profile_arguments(target.get('profile'))],folder,'source')['data']
    state={'format_version':1,'id':identity,'project':str(project),'target_name':target_name,'target':target,
           'source_revision':info.get('revision',info.get('source_revision')),'application':info.get('application',{}),'resolved_title':info.get('resolved',{}).get('title',read(project/'project.json').get('title','Langmake')),'engine':str(engine),'engine_sha256':sha(engine),
           'created_at_ms':now(),'status':'pending','stop_after':stop_after,'phases':{},'attempts':[]}
    write(folder/'operation.json',state)
    return execute(project,folder,state)


def inspect_operation(project,identity):
    if not isinstance(identity,str) or len(identity)!=32 or any(c not in '0123456789abcdef' for c in identity):
        raise WorkflowError('invalid_operation','Operation IDs are the 32-character identities returned by deploy.')
    folder=within(local(project,'operations')/identity, project)
    state=read(folder/'operation.json')
    if not isinstance(state,dict) or state.get('project')!=str(project) or state.get('id')!=identity or state.get('format_version')!=1:
        raise WorkflowError('operation_owner','Operation belongs to a different game.')
    return folder,state


def resume(project,identity,end_play=False):
    folder,state=inspect_operation(project,identity)
    if state.get('status')=='complete':
        return summary(folder,state)
    if state.get('stop_after')!='build': protect_play(project,state['target_name'],end_play,identity if state['phases'].get('launch',{}).get('status')=='complete' else None)
    if state['target']!=selected(project,state['target_name']):
        raise WorkflowError('target_changed','Target configuration changed; start a new deployment instead of resuming this operation.')
    within(state['engine'])
    if sha(state['engine'])!=state['engine_sha256']:
        raise WorkflowError('runtime_changed','Runtime changed since this operation; start a new operation.')
    current=native(state['engine'],['inspect','--project',project,*profile_arguments(state['target'].get('profile'))],folder,'resume-source')['data']
    if current.get('revision',current.get('source_revision'))!=state['source_revision']:
        raise WorkflowError('source_changed','Game sources changed; create a new operation so the existing evidence remains reproducible.')
    for phase in ('install','launch','save_progress'):
        if state['phases'].get(phase,{}).get('status')=='running':
            raise WorkflowError('mutation_uncertain','The previous '+phase+' lost its final result. Inspect the device before starting another deployment; it will not be replayed automatically.',operation_id=identity)
    return execute(project,folder,state)


def summary(folder,state):
    blocking=state.get('blocking')
    if blocking:
        blocking={key:blocking[key] for key in ('code','message','request_id','receipt_path','slot','path','compatibility','next_step') if key in blocking}
    result={'ok':state['status']=='complete','operation_id':state['id'],'status':state['status'],
            'record':str(folder/'operation.json'),'phases':{k:v['status'] for k,v in state['phases'].items()},
            'artifact':state.get('artifact'),'blocking':blocking}
    if state.get('native_operation'):
        result['native_operation']=state['native_operation']
    return result


def execute(project,folder,state):
    with deployment_lock(project):
        if read(folder/'operation.json')!=state:
            raise WorkflowError('operation_changed','Another worker advanced this operation; inspect its latest result before resuming.')
        if state.get('stop_after')!='build':
            protect_play(project,state['target_name'],operation_id=state['id'] if state['phases'].get('launch',{}).get('status')=='complete' else None)
        return _execute(project,folder,state)


def _execute(project,folder,state):
    target=state['target']; platform=target['platform']; engine=Path(state['engine'])
    attempt=folder/('attempt-%04d-' % (len(state['attempts'])+1) + uuid.uuid4().hex[:8]);attempt.mkdir(exist_ok=False)
    adapter=None
    state['attempts'].append({'started_at_ms':now(),'directory':str(attempt)});state['status']='running';state.pop('blocking',None)
    def save():write(folder/'operation.json',state)
    def reserve_human_session():
        if target.get('role','human')=='human':
            write(play_record(project),{'active':True,'target':state['target_name'],'operation_id':state['id'],
                  'started_at_ms':now(),'artifact':state['artifact'],'application_id':identifier,'target_config':target,
                  'note':'Confirm the human finished before a disruptive operation. Launch observation may still be pending.'})
    def phase(name,call):
        prior=state['phases'].get(name,{})
        if prior.get('status')=='complete':return prior.get('result')
        state['phases'][name]={'status':'running','started_at_ms':now()};save()
        value=call()
        state['phases'][name].update(status='complete',finished_at_ms=now(),result=value);save()
        return value
    save()
    try:
        adapter=Platform(target,folder) if platform!='windows' else None
        profile=profile_arguments(target.get('profile'))
        phase('validate',lambda:native(engine,['validate','--project',project,*profile],attempt,'validate')['data'])
        phase('scripts',lambda:native(engine,['check','--project',project,*profile],attempt,'check')['data'])
        application=dict(state.get('application',{}))
        application.update(application.get('platforms',{}).get(platform,{}))
        identifier=target.get('application_id') or application.get('platforms',{}).get(platform,{}).get('id') or application.get('id')
        if platform!='windows' and not identifier:
            raise WorkflowError('application_identity_required','Set portable application.id or this game target\'s explicit application_id before deployment.')
        if target.get('role')=='verification':
            verify_id=target.get('verification_application_id')
            if not verify_id or verify_id==identifier:
                raise WorkflowError('verification_identity_required','A verification target requires a distinct verification_application_id to protect human sessions.')
            identifier=verify_id
        state['application_id']=identifier;save()
        if platform=='windows':
            def build_windows():
                output=attempt/'player';output.mkdir()
                cooked=native(engine,['cook','--project',project,'--target','windows','--output',output/'game',*profile],attempt,'cook')['data']
                runtime=output/'runtime';runtime.mkdir();shutil.copy2(engine,runtime/'langmake.exe')
                for name in ('shaders','fonts'):
                    shutil.copytree(engine.parent/name,runtime/name)
                for file in engine.parent.glob('*.dll'):shutil.copy2(file,runtime/file.name)
                for name in ('LICENSE','THIRD_PARTY_NOTICES.md'):
                    shutil.copy2(ROOT/name,output/name)
                shutil.copytree(ROOT/'licenses',output/'licenses')
                command([sys.executable,str(ROOT/'scripts/windows/windows.py'),'apply','--project',str(output/'game/project.json'),'--executable',str(runtime/'langmake.exe'),'--source-executable',str(engine),'--configuration',target.get('configuration','debug')],attempt,'windows-resources')
                launcher=output/'Play.cmd'
                launcher.write_bytes(b'@echo off\r\ncd /d "%~dp0"\r\nruntime\\langmake.exe play --project game\r\n')
                return {'ok':True,'directory':str(output),'launcher':str(launcher),'packaged_revision':cooked['packaged_revision'],'executable_sha256':sha(runtime/'langmake.exe')}
            built=phase('build',build_windows);state['artifact']=built['directory'];save()
        elif platform=='ios':
            def stage():
                args=['stage','--project',str(project),'--bundle-id',identifier,'--label',application.get('label',state.get('resolved_title','Langmake')),
                      '--configuration',target.get('configuration','debug'),'--host-build',str(engine.parent),'--output',str(attempt/'stage'),*profile]
                if target.get('orientation'):args+=['--orientation',target['orientation']]
                return command([sys.executable,str(ROOT/'scripts/ios/ios.py'),*args],attempt,'stage',timeout=180)['data']
            staged=phase('stage',stage)['staged']
            if adapter.remote:
                adapter.prepare()
                def transfer():
                    archive=attempt/'stage.zip';stage_zip(Path(staged),archive)
                    uploaded=adapter.upload(archive)
                    stage=adapter.remote_folder+'/stage-'+attempt.name;extract_remote(adapter,uploaded,stage)
                    adapter.call(['verify-stage','--staged',stage])
                    return {'stage':stage,'archive_sha256':sha(archive)}
                staged=phase('transfer',transfer)['stage']
            def build_ios():
                if state.get('native_operation',{}).get('resumable'):
                    args=['resume','--operation',state['native_operation']['operation_id']]
                else:
                    output=(adapter.remote_folder+'/app-'+attempt.name) if adapter.remote else str(attempt/'app')
                    args=['build','--staged',staged,'--destination',target['destination'],'--arch',target.get('abi','arm64'),'--output',output]
                    if target['destination']=='device':
                        args+=['--team-id',target['team_id'],'--device',target['device']]
                        if target.get('allow_provisioning_updates'):args+=['--allow-provisioning-updates']
                        if target.get('register_device'):args+=['--register-device']
                result=adapter.call(args,timeout=7200,check=False)
                data=result.get('data') if isinstance(result.get('data'),dict) else {}
                if data.get('operation_id'):
                    state['native_operation']={key:data[key] for key in ('operation_id','operation_path','status','resumable','blocking','helper') if key in data};save()
                if result['exit_code'] or not data.get('ok'):
                    raise WorkflowError('platform_build_blocked','Platform build did not complete; resume this recorded operation after resolving the reported prerequisite.',native=data,result=result)
                return data
            built=phase('build',build_ios);state['artifact']=built['app'];state['storage_id']=built['storage_id'];save()
        else:
            def build_android():
                args=['build','--project',str(project),'--application-id',identifier,'--host-build',str(engine.parent),
                      '--abi',target['abi'],'--configuration',target.get('configuration','debug'),'--output',str(attempt/'game.apk'),*profile]
                if application.get('label'):args+=['--label',application['label']]
                if target.get('orientation'):args+=['--orientation',target['orientation']]
                if target.get('prebuilt_player'):args+=['--player-dir',str(within(target['prebuilt_player']))]
                return adapter.call(args,timeout=3600)['data']
            built=phase('build',build_android);state['artifact']=built['apk'];state['storage_id']=built['storage_id'];save()
        state['packaged_revision']=built['packaged_revision'];save()
        if state['stop_after']=='build':
            state['status']='complete';save();return summary(folder,state)
        if platform!='windows':
            # Preserve manual checkpoint and automatic recovery independently if
            # a previous deployment of this same identity is known and running.
            latest_path=within(project/'.langmake/deployments'/ (state['target_name']+'.json'),project)
            def backup():
                if not latest_path.is_file():return {'ok':True,'status':'first_recorded_install'}
                previous=read(latest_path)
                if not isinstance(previous,dict):raise WorkflowError('invalid_deployment_record','Previous deployment record must be an object.')
                if previous.get('application_id')!=identifier:return {'ok':True,'status':'different_identity'}
                if not isinstance(previous.get('target_config'),dict) or endpoint(previous['target_config'])!=endpoint(target):
                    raise WorkflowError('target_changed','Previous deployment used a different device or host; its save history cannot be inferred for this target. Configure a separate target name for the new device.')
                if previous.get('storage_id')!=state['storage_id']:
                    raise WorkflowError('storage_identity_changed','The new game uses a different save directory. Explicitly migrate the old progress before installing.')
                if previous.get('target_config',{}).get('configuration','debug')=='release':
                    if previous.get('packaged_revision')!=state['packaged_revision']:
                        raise WorkflowError('progress_unverifiable','The existing release app does not expose private saves. Obtain an explicit backup and validate its migration before updating through this workflow.')
                    return {'ok':True,'status':'unavailable_release_private_transport','verified_compatibility':False,'note':'Same content revision; installation retains app data. Private saves cannot be inspected in this release app.'}
                observed=adapter.inspect(identifier,previous['storage_id'])
                data=observed.get('data');live=data.get('live',{}) if isinstance(data,dict) and data.get('ok') is True and observed.get('exit_code')==0 else {}
                result={'ok':True,'status':'not_running','files':[]}
                slot='preupdate_'+state['id'][:12]
                if valid_live(live) or state['phases'].get('save_progress',{}).get('status')=='complete':
                    receipt=phase('save_progress',lambda:adapter.control(identifier,previous['storage_id'],live,'save',slot=slot))
                    out=attempt/'backup'/ (slot+'.json')
                    adapter.pull(identifier,previous['storage_id'],'saves/'+slot+'.json',out)
                    result.update(status='manual_checkpoint_preserved',receipt=receipt['receipt'])
                    result['files'].append({'kind':'manual','path':str(out),'sha256':sha(out)})
                for name in ('_ios_resume','_android_resume'):
                    if (platform=='ios')!=(name=='_ios_resume'):continue
                    out=attempt/'backup'/ (name+'.json')
                    copied=adapter.pull(identifier,previous['storage_id'],'saves/'+name+'.json',out,missing_ok=True)
                    if copied:result['files'].append({'kind':'automatic_recovery','path':str(out),'sha256':sha(out)})
                return result
            preserved=phase('preserve_progress',backup)
            if preserved.get('files'):
                def compatibility():
                    # Assess the exact target cook, whose revision differs from authored sources.
                    cooked=attempt/'compatibility-game'
                    manifest=native(engine,['cook','--project',project,'--target',platform,'--output',cooked,*profile],attempt,'compatibility-cook')['data']
                    if manifest.get('packaged_revision')!=state['packaged_revision']:
                        raise WorkflowError('artifact_revision_mismatch','Compatibility cook differs from the built artifact; installation was not attempted.')
                    checks=[]
                    for index,item in enumerate(preserved['files']):
                        source=within(item['path'],project)
                        if sha(source)!=item['sha256']:raise WorkflowError('backup_changed','Saved progress backup changed since it was recorded.')
                        assessed=native(engine,['save-check','--project',cooked,'--file',source],attempt,'save-check-'+str(index))['data']
                        restored=native(engine,['simulate','--project',cooked,'--load-save',source,'--steps','0','--summary'],attempt,'save-restore-'+str(index),check=False)
                        if restored.get('exit_code') or not isinstance(restored.get('data'),dict) or restored['data'].get('ok') is not True:
                            raise WorkflowError('save_incompatible','Saved progress could not be restored by the new game. Author and test an explicit migration before installing.',path=str(source),compatibility=assessed,result=restored)
                        checks.append({'path':str(source),'compatibility':assessed,'restore_verified':True,'migration_verified':assessed.get('compatible') is not True})
                    return {'ok':True,'checks':checks}
                phase('save_compatibility',compatibility)
            flag='--app' if platform=='ios' else '--apk'
            install_args=['install',*adapter.base(),flag,state['artifact']]
            if platform=='android':install_args+=adapter.identity(identifier)
            phase('install',lambda:adapter.call(install_args,timeout=180)['data'])
            deployment={key:state[key] for key in ('id','application_id','storage_id','artifact','packaged_revision','source_revision','engine_sha256')}
            deployment.update(target_config=target,target_sha256=fingerprint(target),installed_at_ms=now())
            write(latest_path,deployment)
        if state['stop_after']=='install':
            state['status']='complete';save();return summary(folder,state)
        if platform=='windows':
            def launch_windows():
                executable=Path(state['artifact'])/'runtime/langmake.exe'
                process=subprocess.Popen([str(executable),'play','--project',str(Path(state['artifact'])/'game')],cwd=ROOT,
                                         stdout=(attempt/'player.log').open('xb'),stderr=subprocess.STDOUT)
                return {'ok':True,'pid':process.pid,'mode':'play'}
            phase('launch',launch_windows)
            reserve_human_session()
        else:
            if target.get('configuration','debug')=='debug':
                def prelaunch():
                    observed=adapter.inspect(identifier,state['storage_id']);data=observed.get('data');live=data.get('live',{}) if isinstance(data,dict) else {}
                    return {'session':live.get('session') if isinstance(live.get('session'),str) else None}
                before=phase('prelaunch',prelaunch)
            else: before={'session':None}
            phase('launch',lambda:adapter.launch(identifier))
            reserve_human_session()
            if target.get('configuration','debug')=='release':
                phase('observe',lambda:{'ok':True,'status':'unavailable_release_private_transport','verified_live':False,'note':'Release launch was acknowledged; native private state inspection is disabled. Human verification remains required.'})
            else:
                observed=phase('observe',lambda:adapter.fresh_live(identifier,state['storage_id'],previous=before['session'],revision=state['packaged_revision']))
                if observed.get('mode')!='play':raise WorkflowError('unexpected_mode','Human deployment must launch in ordinary player mode.')
        state['status']='complete';state['completed_at_ms']=now();save()
    except Exception as error:
        state['status']='blocked';state['blocking']=failure(error);state['attempts'][-1]['error']=failure(error);save()
    finally:
        state['attempts'][-1]['finished_at_ms']=now();save()
        write(attempt/'operation-result.json',state)
    return summary(folder,state)
