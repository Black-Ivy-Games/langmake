"""Workspace-local, pinned authoring dependency and instrument installation."""
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.parse
import urllib.request

SFIZZ_COMMIT = '4e70dc0bef53b41f2853ed46e26f5911114c92d0'


def pack_manifest():
    return json.loads((Path(__file__).parent/'packs/vsco-ce.json').read_text(encoding='utf-8'))


def install_pack(directory):
    directory = Path(directory).resolve(); manifest = pack_manifest()
    def fetch(entry):
        target = directory/entry['path']
        if not target.resolve().is_relative_to(directory):
            raise ValueError('pack manifest escaped destination')
        if target.is_file() and hashlib.sha256(target.read_bytes()).hexdigest() == entry['sha256']:
            return
        request = urllib.request.Request(manifest['base_url']+urllib.parse.quote(entry['path']),headers={'User-Agent':'LangmakeMusic/1'})
        with urllib.request.urlopen(request, timeout=60) as response:
            data = response.read(entry['bytes']+1)
        if len(data) != entry['bytes'] or hashlib.sha256(data).hexdigest() != entry['sha256']:
            raise ValueError('pack download hash/size mismatch: '+entry['path'])
        target.parent.mkdir(parents=True,exist_ok=True)
        temp = target.with_suffix(target.suffix+'.partial'); temp.write_bytes(data); os.replace(temp,target)
    directory.mkdir(parents=True,exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(fetch,manifest['files']))
    (directory/'pack.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    return {'ok':True,'pack':manifest['id'],'directory':str(directory),'bytes':sum(f['bytes'] for f in manifest['files']),'instruments':list(manifest['instruments']),'license':manifest['license']}


def load_pack(directory, verify=False):
    directory = Path(directory).resolve()
    path = directory/'pack.json'
    if not path.is_file():
        raise ValueError('Instrument pack missing. Run langmake music pack-install --directory PATH, then use --pack PATH')
    pack = json.loads(path.read_text(encoding='utf-8'))
    canonical = pack_manifest()
    if pack != canonical:
        raise ValueError('Pack metadata differs from the pinned manifest; reinstall the pack')
    for entry in pack['files']:
        p = directory/entry['path']
        if not p.is_file() or p.stat().st_size != entry['bytes'] or (verify and hashlib.sha256(p.read_bytes()).hexdigest() != entry['sha256']):
            raise ValueError('Missing or changed instrument file: '+entry['path'])
    pack['_directory'] = directory
    return pack


def setup(root, jobs=4):
    root = Path(root).resolve(); source=root/'.deps/source/sfizz'; build=root/'.cache/expressive-audio/sfizz-build'; target=root/'.deps/music/bin'
    for tool in ['git','cmake']:
        if not shutil.which(tool):
            raise ValueError(f'{tool} is required on PATH to build optional music authoring tools; no global installation is performed')
    env = os.environ.copy(); temporary=root/'.cache/tmp'; temporary.mkdir(parents=True,exist_ok=True)
    env.update(TEMP=str(temporary),TMP=str(temporary),TMPDIR=str(temporary))
    def run(command):
        subprocess.run(command,check=True,cwd=root,env=env,stdout=sys.stderr,stderr=sys.stderr)
    if not source.exists():
        source.parent.mkdir(parents=True,exist_ok=True)
        run(['git','clone','--depth','1','--branch','1.2.3','--recurse-submodules','--shallow-submodules','https://github.com/sfztools/sfizz.git',str(source)])
    revision = subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()
    if revision != SFIZZ_COMMIT:
        raise ValueError('sfizz checkout does not match pinned commit; do not overwrite an unrelated checkout')
    submodules = subprocess.check_output(['git','-C',str(source),'submodule','status','--recursive'],text=True).splitlines()
    if any(line and line[0] != ' ' for line in submodules):
        raise ValueError('sfizz submodules differ from the pinned source graph; initialize the recorded submodules without modifying their code')
    if subprocess.check_output(['git','-C',str(source),'status','--porcelain','--untracked-files=no'],text=True).strip():
        raise ValueError('sfizz or its submodules contain tracked edits; preserve them and use a clean pinned checkout')
    command=['cmake','-S',str(source),'-B',str(build),'-DCMAKE_BUILD_TYPE=Release','-DSFIZZ_JACK=OFF','-DSFIZZ_RENDER=OFF','-DSFIZZ_SHARED=ON','-DENABLE_LTO=OFF','-DSFIZZ_TESTS=OFF','-DSFIZZ_DEMOS=OFF','-DSFIZZ_DEVTOOLS=OFF','-DSFIZZ_USE_SNDFILE=OFF']
    if shutil.which('ninja') and not (build/'CMakeCache.txt').exists():
        command += ['-G','Ninja']
    run(command);run(['cmake','--build',str(build),'--config','Release','--parallel',str(jobs),'--target','sfizz_shared'])
    target.mkdir(parents=True,exist_ok=True)
    names=['libsfizz.dll','sfizz.dll','libsfizz.dylib','libsfizz.so','libsfizz.so.1','libsfizz.so.1.2.3']
    found=[]
    for name in names:
        for path in build.rglob(name):
            if path.is_file():
                shutil.copy2(path,target/name);found.append(name);break
    if not found:
        raise ValueError('sfizz build succeeded but its shared library was not found')
    # GCC redistributable DLLs are copied only from the selected compiler's own
    # directory; never search unrelated user directories or modify global PATH.
    if sys.platform=='win32' and shutil.which('g++'):
        compiler=Path(shutil.which('g++')).parent
        for name in ['libgcc_s_seh-1.dll','libstdc++-6.dll','libwinpthread-1.dll']:
            if (compiler/name).is_file():
                shutil.copy2(compiler/name,target/name)
    report={'ok':True,'sampler':'sfizz','version':'1.2.3','commit':revision,'submodules':submodules,'configure_arguments':command,'directory':str(target),'files':[{'path':p.name,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(target.iterdir()) if p.is_file()]}
    (root/'.deps/music/build-report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return report
