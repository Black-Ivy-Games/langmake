"""Platform adapters and bounded session-bound receipt delivery."""
from __future__ import annotations
import json
from pathlib import Path, PurePosixPath
import re
import shlex
import subprocess
import sys
import time
import uuid
import shutil
from .common import ROOT, WorkflowError, command, now, read, sha, within, write


def valid_live(live):
    if not isinstance(live, dict) or live.get('running') is not True:
        return False
    if any(not isinstance(live.get(key), str) or not live[key] or len(live[key]) > 256 for key in ('session', 'source_revision')):
        return False
    stamp = live.get('published_at_ms')
    return type(stamp) in (int, float) and -120000 < now() - stamp < 15000


class Platform:
    def __init__(self, target, folder):
        from .targets import validate
        validate(target)
        self.target, self.folder, self.counter = target, within(folder), 0
        self.logs = self.folder / ('transport-' + uuid.uuid4().hex)
        self.logs.mkdir(parents=True, exist_ok=False)
        self.platform = target['platform']
        self.host = target.get('host', {'transport':'local'})
        self.remote = self.host.get('transport') == 'ssh'
        self.remote_folder = None
        if self.remote:
            config = Path(self.host['config'])
            self.config = within(config if config.is_absolute() else ROOT / config)
            if not self.config.is_file():
                raise WorkflowError('ssh_config_missing', 'The SSH configuration explicitly selected for this game is missing.', path=str(self.config))
            self.remote_folder = str(PurePosixPath(self.host['workspace']) / self.target['game_directory'] / '.langmake/workflow' / self.folder.name)

    def label(self, prefix):
        self.counter += 1
        return '%s-%04d' % (prefix, self.counter)

    def ssh_arguments(self, text):
        return ['ssh', '-F', str(self.config), '-o', 'BatchMode=yes', '-o', 'StrictHostKeyChecking=yes',
                '-o', 'ConnectTimeout=10', self.host['alias'], text]

    def remote_command(self, args):
        prefix = 'cd ' + shlex.quote(self.host['workspace']) + ' && '
        if self.host.get('environment_script'):
            prefix += '. ' + shlex.quote('./' + self.host['environment_script']) + ' && '
        return prefix + shlex.join([str(v) for v in args])

    def prepare(self):
        if not self.remote:
            return
        # User selected this game directory explicitly. No default host/path and
        # no broad source synchronization or cleanup is performed by deployment.
        script = 'import pathlib,json,sys; root=pathlib.Path(sys.argv[1]).resolve(); p=pathlib.Path(sys.argv[2]); assert p.is_absolute() and p.resolve().is_relative_to(root), "Remote path escapes workspace"; assert not any(v.is_symlink() for v in [p,*p.parents] if v.is_relative_to(root)), "Remote output cannot use symlinks"; p.mkdir(parents=True,exist_ok=True); print(json.dumps({"ok":True,"directory":str(p)}))'
        args = [self.host.get('python','python3'), '-c', script, self.host['workspace'], self.remote_folder]
        command(self.ssh_arguments(self.remote_command(args)), self.logs, self.label('prepare'), timeout=30)

    def upload(self, path, name=None):
        path = within(path)
        if not self.remote:
            return str(path)
        name = name or path.name
        if '/' in name or '\\' in name or not re.fullmatch(r'[A-Za-z0-9_.-]{1,128}', name):
            raise WorkflowError('invalid_transfer_name', 'Remote transfer filename must be a simple portable name.')
        destination = self.remote_folder + '/' + uuid.uuid4().hex + '-' + name
        # Stream bytes over SSH stdin; no scp remote-shell filename expansion.
        script = 'import pathlib,hashlib,json,sys,shutil; p=pathlib.Path(sys.argv[1]); f=p.open("xb"); shutil.copyfileobj(sys.stdin.buffer,f); f.close(); print(json.dumps({"ok":True,"sha256":hashlib.sha256(p.read_bytes()).hexdigest()}))'
        value = command(self.ssh_arguments(self.remote_command([self.host.get('python','python3'),'-c',script,destination])),
                        self.logs,self.label('upload'),timeout=120,stdin=path)['data']
        if value.get('sha256') != sha(path):
            raise WorkflowError('transfer_hash_mismatch', 'Uploaded bytes differ from local input.')
        return destination

    def download(self, remote, output):
        output = within(output)
        candidate, root = PurePosixPath(remote), PurePosixPath(self.remote_folder)
        if not candidate.is_absolute() or '..' in candidate.parts or not candidate.is_relative_to(root):
            raise WorkflowError('invalid_transfer_path', 'Download must stay inside this operation on the selected remote host.')
        output.parent.mkdir(parents=True,exist_ok=True)
        script = 'import pathlib,sys,shutil; p=pathlib.Path(sys.argv[1]); assert p.stat().st_size<=268435456, "File exceeds transfer limit"; f=p.open("rb"); shutil.copyfileobj(f,sys.stdout.buffer)'
        value = command(self.ssh_arguments(self.remote_command([self.host.get('python','python3'),'-c',script,remote])),
                        self.logs,self.label('download'),timeout=120,check=False)
        if value['exit_code']:
            raise WorkflowError('transfer_failed','SSH file download failed.',result=value)
        if output.exists():
            raise WorkflowError('evidence_exists', 'Refusing to overwrite downloaded evidence: ' + str(output))
        shutil.copyfile(value['stdout'], output)

    def call(self, args, timeout=120, check=True):
        script = 'scripts/%s/%s.py' % (self.platform,self.platform)
        if self.remote:
            argv = self.ssh_arguments(self.remote_command([self.host.get('python','python3'),script,*args]))
        else:
            argv = [sys.executable,str(ROOT / script),*args]
        return command(argv,self.logs,self.label(self.platform),timeout=timeout,check=check)

    def base(self):
        if self.platform == 'ios':
            return ['--destination',self.target['destination'],'--device',self.target['device']]
        return ['--serial',self.target['serial']]

    def identity(self, bundle):
        return ['--bundle-id' if self.platform == 'ios' else '--application-id',bundle]

    def inspect(self, bundle, storage):
        return self.call(['inspect',*self.base(),*self.identity(bundle),'--storage-id',storage],timeout=30,check=False)

    def pull(self,bundle,storage,path,output,missing_ok=False):
        output = within(output)
        destination = self.remote_folder + '/' + ('pull-' + uuid.uuid4().hex + output.suffix) if self.remote else str(output)
        result = self.call(['pull',*self.base(),*self.identity(bundle),'--storage-id',storage,'--path',path,'--output',destination],timeout=30,check=False)
        if result['exit_code'] or not isinstance(result.get('data'), dict) or result['data'].get('ok') is not True:
            data = result.get('data')
            if missing_ok and isinstance(data, dict) and data.get('ok') is False and data.get('code') == 'file_not_found':
                return None
            message = json.dumps(data) if data else Path(result['stderr']).read_text(encoding='utf-8',errors='replace')[-4000:]
            missing = re.search(r'no such file|file[^\n]*(?:does not exist|not found|is missing|could not be found)|couldn.t be opened because there is no such file', message, re.I)
            infrastructure = re.search(r'(?:device|application|app|command|executable|python)[^\n]{0,80}(?:not found|does not exist|unavailable)|permission denied|unauthorized|connection refused', message, re.I)
            if missing_ok and missing and not infrastructure:
                return None
            raise WorkflowError('device_read_failed','Device file read failed; it was not parsed as a receipt.',result=result)
        if self.remote:
            self.download(destination,output)
        if not output.is_file():
            raise WorkflowError('missing_transfer','Transport did not produce the requested file.')
        return output

    def push(self,bundle,storage,path,source):
        source = self.upload(source) if self.remote else str(source)
        return self.call(['push',*self.base(),*self.identity(bundle),'--storage-id',storage,'--path',path,'--file',source])['data']

    def launch(self,bundle,flags=()):
        args=['launch',*self.base(),*self.identity(bundle)]
        if self.platform == 'android' and flags:
            args+=['--debug-args']
        return self.call([*args,*flags],timeout=60)['data']

    def control(self,bundle,storage,live,action,timeout=30,**fields):
        if not valid_live(live):
            raise WorkflowError('stale_session','A fresh running observation is required before dispatching a command.')
        if set(fields) & {'id', 'session', 'action', 'expires_at_ms'}:
            raise WorkflowError('invalid_request', 'Request identity/session/action fields cannot be overridden.')
        if action not in ('save','load','snapshot','capture','pause','resume','reset','step') or type(timeout) not in (int, float) or not 0 < timeout <= 300:
            raise WorkflowError('invalid_request', 'Select a supported native action and a timeout in (0,300] seconds.')
        request_id=str(uuid.uuid4().int)
        path=self.folder/('request-'+request_id+'.json')
        request={'id':request_id,'session':live['session'],'action':action,'expires_at_ms':now()+timeout*1000,**fields}
        write(path,request)
        source=self.upload(path) if self.remote else str(path)
        # Exactly one mutation dispatch. A native consumer may remove the inbox
        # file before the device-copy tool acknowledges it. Its strictly bound
        # receipt is authoritative even if that copy acknowledgment is lost.
        delivery_warning = None
        try:
            sent=self.call(['control',*self.base(),*self.identity(bundle),'--storage-id',storage,'--file',source],timeout=30,check=False)
        except Exception as error:
            delivery_warning = {'code':'delivery_acknowledgment_lost', 'message':str(error),
                                'exception_type':type(error).__name__, 'details':getattr(error,'details',{})}
        else:
            if sent['exit_code'] or not isinstance(sent.get('data'), dict) or sent['data'].get('ok') is not True:
                delivery_warning = {'code':'delivery_acknowledgment_unconfirmed',
                                    'message':'Device-copy acknowledgment failed; only a matching native receipt can establish the outcome.', 'result':sent}
        delivery_evidence = None
        if delivery_warning is not None:
            delivery_evidence = self.folder/('delivery-'+request_id+'.json')
            write(delivery_evidence, {'request_id':request_id,'request':str(path), 'session':live['session'],
                                     'action':action,'source_revision':live['source_revision'], 'dispatch_count':1,
                                     'warning':delivery_warning})
        uncertain = {'request_id':request_id, 'receipt_path':'results/'+request_id+'.json', 'request':str(path)}
        if delivery_evidence is not None:
            uncertain['delivery_warning'] = {'code':delivery_warning['code'], 'record':str(delivery_evidence)}
        receipt_path=self.folder/('receipt-'+request_id+'.json')
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            try:
                receipt=self.pull(bundle,storage,'results/'+request_id+'.json',receipt_path,missing_ok=True)
            except Exception as error:
                raise WorkflowError('receipt_observation_failed', 'Command was dispatched but its receipt could not be read; do not resend the mutation.', **uncertain) from error
            if receipt:
                try:
                    result=read(receipt)
                except (ValueError, OSError, WorkflowError) as error:
                    raise WorkflowError('malformed_receipt', 'Receipt was not a valid bounded JSON document; mutation was not repeated.', request_id=request_id, receipt=str(receipt_path)) from error
                if not isinstance(result, dict) or result.get('id')!=request_id:
                    raise WorkflowError('receipt_mismatch','Receipt has the wrong request identity.')
                if result.get('ok') is not True:
                    raise WorkflowError('native_command_failed',result.get('diagnostic','Native command rejected.'),receipt=str(receipt_path))
                if result.get('source_revision')!=live.get('source_revision'):
                    raise WorkflowError('revision_mismatch','Command receipt belongs to a different content revision.')
                # Ordinary acknowledgments carry their session/action. Legacy
                # capture receipts bind these through the mandatory sidecar.
                if (action!='capture' or 'session' in result) and result.get('session')!=live['session']:
                    raise WorkflowError('session_mismatch','Command receipt belongs to another play session.', request_id=request_id)
                if (action!='capture' or 'action' in result) and result.get('action')!=action:
                    raise WorkflowError('receipt_mismatch', 'Receipt names a different action.', request_id=request_id)
                if action=='capture':
                    native_image=result.get('image')
                    if not isinstance(native_image,str) or not native_image.startswith('/') or any(ord(c)<32 for c in native_image) or '\\' in native_image or '..' in PurePosixPath(native_image).parts or not native_image.endswith('/evidence/'+request_id+'.png'):
                        raise WorkflowError('capture_mismatch','Capture receipt image does not identify this request.',request_id=request_id)
                    image=self.folder/('capture-'+request_id+'.png')
                    self.pull(bundle,storage,'evidence/'+request_id+'.png',image)
                    sidecar=image.with_suffix('.png.json')
                    self.pull(bundle,storage,'evidence/'+request_id+'.png.json',sidecar)
                    state=read(sidecar)
                    if not isinstance(state, dict) or state.get('session')!=live['session'] or state.get('source_revision')!=live['source_revision'] or state.get('image')!=native_image or type(state.get('tick')) is not int or state['tick']<0 or ('tick' in result and result['tick']!=state['tick']):
                        raise WorkflowError('capture_mismatch','Capture sidecar does not match the requested image, session, revision or tick.')
                    result.update(action='capture',image=str(image),sidecar=str(sidecar),tick=state['tick'],session=state['session'])
                response = {'ok':True,'request_id':request_id,'receipt':str(receipt_path),'result':result}
                if delivery_evidence is not None:
                    response['delivery_warning'] = {**uncertain['delivery_warning'], 'resolved_by':'verified_native_receipt'}
                return response
            time.sleep(.25)
        if delivery_warning is not None:
            raise WorkflowError('delivery_uncertain','Delivery acknowledgment failed and no matching receipt arrived; the command was not repeated.',**uncertain)
        raise WorkflowError('receipt_timeout','No matching receipt arrived; the command was not repeated.',**uncertain)

    def fresh_live(self,bundle,storage,previous=None,timeout=30,revision=None):
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            result=self.inspect(bundle,storage)
            data = result.get('data')
            live=data.get('live', {}) if isinstance(data, dict) and data.get('ok') is True and result.get('exit_code') == 0 else {}
            if valid_live(live) and live['session']!=previous:
                if revision and live.get('source_revision')!=revision:
                    raise WorkflowError('revision_mismatch','Running content differs from the selected artifact.')
                return live
            time.sleep(.5)
        raise WorkflowError('observation_timeout','No fresh running game observation arrived; no launch was repeated.')
