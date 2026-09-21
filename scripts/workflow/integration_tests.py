"""Exercise the public native facade against an isolated, newly authored game."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import uuid
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--engine',type=Path,default=ROOT/'build/langmake.exe');parser.add_argument('--graphics',action='store_true');args=parser.parse_args()
    folder=ROOT/'.cache/workflow-integration'/uuid.uuid4().hex;folder.mkdir(parents=True)
    engine=args.engine.resolve();project=folder/'garden';calls=[]
    def run(*arguments,ok=True):
        result=subprocess.run([str(engine),*map(str,arguments)],cwd=ROOT,capture_output=True,text=True,timeout=180)
        try:value=json.loads(result.stdout)
        except ValueError:raise AssertionError((arguments,result.stdout,result.stderr))
        if (result.returncode==0)!=ok or value.get('ok',True)!=ok:raise AssertionError((arguments,value,result.stderr))
        calls.append({'command':arguments[0],'ok':value.get('ok'),'expected_ok':ok})
        return value
    run('new','--template','workflow-2d','--output',project)
    original=json.loads((ROOT/'examples/workflow-2d/project.json').read_text());game=json.loads((project/'project.json').read_text())
    assert game['project_id']!=original['project_id'] and game['application']['id']!=original['application']['id']
    assert (project/'README.md').is_file()
    context=run('context','--project',project)
    assert Path(context['runtime']['path']).samefile(engine) and context['local_configuration']['targets']=={}
    assert context['runtime']['version']=='0.5.0' and 'delivery' in context['project']['scenarios']
    run('target','add','--project',project,'--name','phone',ok=False)
    settings=folder/'desktop.json';settings.write_text(json.dumps({'platform':'windows','profile':'desktop'}))
    run('target','add','--project',project,'--name','desktop','--file',settings)
    assert list(run('target','list','--project',project)['targets'])==['desktop']
    run('target','add','--project',project,'--name','desktop','--file',settings,ok=False)
    for profile,scenario in [('desktop','delivery'),('phone','release'),('tablet','delivery')]:
        options=['--graphics'] if args.graphics else []
        result=run('verify','--project',project,'--scenario',scenario,'--profile',profile,*options)
        assert result['human_playtested'] is False and result['physically_input_tested'] is False
        report=json.loads(Path(result['report']).read_text())
        assert all(item['ok'] for item in report['assertions'])
    build=run('deploy','--project',project,'--target','desktop','--stop-after','build')
    assert build['status']=='complete' and Path(build['artifact']).is_dir()
    before=(Path(build['record'])).read_bytes()
    run('operation','resume','--project',project,'--id',build['operation_id'])
    assert Path(build['record']).read_bytes()==before,'completed operation changed on resume'
    brief=project/'BRIEF.md';brief.write_text('Make a small garden prototype. Await human feedback on movement and layout.\n')
    handoff=run('handoff','--project',project,'--brief','BRIEF.md')
    portable=Path(handoff['handoff']).read_text()
    assert str(ROOT) not in portable and 'SSH' not in portable and 'private_key' not in portable
    run('session','show','--project',project)
    run('target','remove','--project',project,'--name','desktop')
    report={'ok':True,'project':str(project),'calls':len(calls),'graphics':args.graphics,'artifact':build['artifact'],'checks':calls}
    (folder/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'ok':True,'report':str(folder/'result.json'),'calls':len(calls)},indent=2))


if __name__=='__main__':main()
