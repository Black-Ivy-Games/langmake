#!/usr/bin/env python3
"""Optional text music authoring CLI. Standalone and `langmake music` entrypoint."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from composition import Composition, pitch, schema
from sampler import library, render
from tooling import install_pack, load_pack, pack_manifest, setup


def read(path):
    p=Path(path)
    if p.stat().st_size>4*1024*1024:
        raise ValueError('Composition JSON limit is 4 MiB')
    def pairs(items):
        result={}
        for k,v in items:
            if k in result:
                raise ValueError('Duplicate JSON key: '+k)
            result[k]=v
        return result
    return json.loads(p.read_text(encoding='utf-8-sig'),object_pairs_hook=pairs)


def audition_project(project, output, engine):
    output=Path(output).resolve();project=Path(project).resolve()
    audition=output/'audition.project.json'
    # References remain relative to the caller's actual game root. A temporary
    # manifest at that root lets the native mixer produce exactly the game mix.
    recipe=(output/'music.json').relative_to(project).as_posix()
    manifest=project/('.music-audition-'+hashlib.sha256(str(output).encode()).hexdigest()[:12]+'.json')
    if manifest.exists():
        raise ValueError('audition manifest already exists; another render may be active')
    script=output/'audition.luau';script.write_text('return {}\n',encoding='utf-8')
    doc={'schema_version':2,'title':'Music audition','dimension':'2d','script':script.relative_to(project).as_posix(),'gravity':[0,0,0],'entities':[], 'assets':{'audition':{'type':'music','path':recipe}}}
    manifest.write_text(json.dumps(doc,indent=2),encoding='utf-8')
    try:
        command=[str(engine),'asset','render','--project',str(manifest),'--id','audition','--output',str(output/'audition.wav')]
        proc=subprocess.run(command,capture_output=True,text=True)
        if proc.returncode:
            raise ValueError('Native audition failed: '+proc.stdout+proc.stderr)
        report=json.loads(proc.stdout)
        (output/'audition-report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        return report
    finally:
        manifest.unlink(missing_ok=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine',type=Path)
    parser.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[2])
    subs=parser.add_subparsers(dest='command',required=True)
    subs.add_parser('schema');subs.add_parser('status');subs.add_parser('instruments')
    s=subs.add_parser('setup');s.add_argument('--jobs',type=int,choices=range(1,9),default=4)
    s=subs.add_parser('pack-install');s.add_argument('--directory',type=Path,required=True)
    s=subs.add_parser('pack-verify');s.add_argument('--pack',type=Path,required=True)
    for command in ['validate','render','transform']:
        s=subs.add_parser(command);s.add_argument('--score',type=Path,required=True)
        if command=='render':
            s.add_argument('--pack',type=Path,required=True);s.add_argument('--project',type=Path,required=True);s.add_argument('--output-dir',type=Path,required=True)
            s.add_argument('--section');s.add_argument('--track')
        elif command=='transform':
            s.add_argument('--output',type=Path,required=True);s.add_argument('--track',required=True);s.add_argument('--section');s.add_argument('--transpose',type=int,default=0);s.add_argument('--velocity',type=float,default=1)
    s=subs.add_parser('example');s.add_argument('--project',type=Path,required=True);s.add_argument('--pack',type=Path,required=True)
    s=subs.add_parser('audition');s.add_argument('--instrument',required=True);s.add_argument('--pack',type=Path,required=True);s.add_argument('--project',type=Path,required=True);s.add_argument('--output-dir',type=Path,required=True)
    s=subs.add_parser('import');s.add_argument('--source',type=Path,required=True);s.add_argument('--project',type=Path,required=True);s.add_argument('--output-dir',type=Path,required=True);s.add_argument('--frames',type=int,required=True);s.add_argument('--bpm',type=float,default=120);s.add_argument('--license',required=True)
    argv=sys.argv[1:]
    # Native command forwards its literal `music` group after global options.
    if 'music' in argv:
        index=argv.index('music')
        if index==0 or (index==2 and argv[0]=='--engine'):
            del argv[index]
    args=parser.parse_args(argv);root=args.root.resolve();engine=args.engine or root/('build/langmake.exe' if os.name=='nt' else 'build/langmake')
    manifest=pack_manifest();instruments=manifest['instruments']
    if args.command=='schema':return schema()
    if args.command=='instruments':return {'ok':True,'pack':'vsco-ce','license':manifest['license'],'download_bytes':sum(f['bytes'] for f in manifest['files']),'instruments':{id:{k:v for k,v in i.items() if k!='regions'} for id,i in instruments.items()},'pitch_notation':'Scientific: C4 = MIDI 60. Articulations use separate instrument IDs or explicit track mappings.'}
    if args.command=='status':
        try:_,p=library(root);available=True;error=None
        except ValueError as e:available=False;p=None;error=str(e)
        return {'ok':True,'sampler_ready':available,'library':p,'detail':error,'native_player_requires_sampler':False,'guide':'docs/30-music-and-sound-guide.md'}
    if args.command=='setup':return setup(root,args.jobs)
    if args.command=='pack-install':return install_pack(args.directory)
    if args.command=='pack-verify':
        p=load_pack(args.pack,verify=True);return {'ok':True,'pack':p['id'],'files_verified':len(p['files'])}
    if args.command=='example':
        destination=args.project.resolve()
        if destination.exists():raise ValueError('Example destination must not exist; existing games are preserved')
        source=root/'examples/music/showcase'
        if not source.is_dir():raise ValueError('Music reference sources are missing from this installation')
        pack=load_pack(args.pack,verify=True)
        shutil.copytree(source,destination)
        reports=[]
        for name in ['lanterns','storm-gate','clockwork-market','chime']:
            composition=Composition(read(root/'examples/music'/(name+'.composition.json')),instruments)
            reports.append(render(root,composition,pack,destination/'audio'/name,destination))
        result=subprocess.run([str(engine),'validate','--project',str(destination)],capture_output=True,text=True)
        if result.returncode:raise ValueError('Example validation failed: '+result.stdout+result.stderr)
        return {'ok':True,'project':str(destination),'play':[str(engine),'play','--project',str(destination)],'renders':reports,'human_audition':'pending'}
    if args.command=='import':
        if args.source.suffix.lower() not in ('.wav','.flac') or args.frames<1 or args.frames>48000*600 or not 20<=args.bpm<=400:
            raise ValueError('Import requires WAV/FLAC, 1..28800000 output frames, bpm 20..400 and recorded source license')
        output=args.output_dir.resolve();project=args.project.resolve()
        if not output.is_relative_to(project) or output.exists():
            raise ValueError('Import output must be a new directory inside project')
        output.mkdir(parents=True);dest=output/('recording'+args.source.suffix.lower());shutil.copy2(args.source,dest)
        recipe={'schema_version':2,'type':'music','title':args.source.stem,'length_frames':args.frames,'bpm':args.bpm,'gain':.7,'tracks':[{'id':'recording','source':dest.relative_to(project).as_posix()}],'provenance':{'license':args.license,'source_sha256':hashlib.sha256(dest.read_bytes()).hexdigest()}}
        (output/'music.json').write_text(json.dumps(recipe,indent=2)+'\n',encoding='utf-8')
        return {'ok':True,'recipe':str(output/'music.json'),'audition':audition_project(project,output,engine)}
    if args.command=='audition':
        if args.instrument not in instruments:raise ValueError('Unknown instrument')
        info=instruments[args.instrument];base=max(info['low'],min(60,info['high']))
        doc={'schema_version':1,'type':'composition','title':info['name']+' audition','length_beats':8,'loop':False,'tracks':[{'id':'instrument','instrument':args.instrument,'notes':[{'beat':i*2,'duration':1.5,'pitch':min(info['high'],base+step),'velocity':velocity} for i,(step,velocity) in enumerate([(0,.35),(4,.55),(7,.75),(12,.95)])]}]}
    else:doc=read(args.score)
    compiled=Composition(doc,instruments)
    if args.command=='validate':return compiled.summary()
    if args.command=='transform':
        if not -48<=args.transpose<=48 or not 0<=args.velocity<=2:raise ValueError('Transpose is -48..48 and velocity multiplier is 0..2')
        if args.output.exists():raise ValueError('Transform output already exists; preserve source and compare revisions')
        selected=compiled.sections.get(args.section) if args.section else {'start':0,'end':compiled.length}
        if not selected:raise ValueError('Unknown section')
        track=next((t for t in doc['tracks'] if t['id']==args.track),None)
        if not track:raise ValueError('Unknown track')
        # Expand only the selected track before editing; shared patterns remain
        # intact for every other part, so a local revision cannot change them.
        expanded=next(t for t in compiled.tracks if t['id']==args.track)
        notes=[]
        for old in expanded['notes']:
            n={k:v for k,v in old.items() if k in ['beat','duration','pitch','articulation']}
            # Use source velocity, not the humanized MIDI event velocity.
            n['velocity']=old['source_velocity']
            if selected['start']<=n['beat']<selected['end']:
                n['pitch']+=args.transpose;n['velocity']=max(.001,min(1,n['velocity']*args.velocity))
            notes.append(n)
        track['notes']=notes;track.pop('arrangement',None);Composition(doc,instruments)
        args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(doc,indent=2)+'\n',encoding='utf-8')
        return {'ok':True,'output':str(args.output.resolve()),'edited_track':args.track,'section':args.section}
    pack=load_pack(args.pack,verify=True)
    report=render(root,compiled,pack,args.output_dir,args.project,getattr(args,'section',None),getattr(args,'track',None))
    report['audition']=audition_project(args.project,args.output_dir,engine)
    return report


if __name__=='__main__':
    try:
        print(json.dumps(main(),indent=2))
    except (ValueError, OSError, KeyError, subprocess.CalledProcessError) as e:
        print(json.dumps({'ok':False,'code':'MUSIC_AUTHORING_FAILED','message':str(e)},indent=2));sys.exit(1)
