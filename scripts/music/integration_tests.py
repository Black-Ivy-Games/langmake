"""Recorded asset/source graph and optional authoring CLI regression checks."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import uuid
import wave

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', type=Path, required=True)
    args = parser.parse_args()
    engine = args.engine.resolve()
    work = ROOT / '.cache/music-integration' / uuid.uuid4().hex
    game = work / 'game'
    game.mkdir(parents=True)

    def write(path, value):
        path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')

    def run(*command, ok=True):
        p = subprocess.run([str(engine)] + [str(x) for x in command], capture_output=True, text=True)
        value = json.loads(p.stdout)
        if (p.returncode == 0) != ok:
            raise AssertionError((command, value, p.stderr))
        return value

    def pcm(sample):
        with wave.open(str(game/'part.wav'), 'wb') as f:
            f.setparams((1, 2, 48000, 0, 'NONE', 'not compressed'))
            f.writeframes(sample.to_bytes(2, 'little', signed=True)*4800)

    pcm(4096)
    original = (game/'part.wav').read_bytes()
    write(game/'music.json', {'schema_version':2,'type':'music','length_frames':4800,
                             'tracks':[{'id':'part','source':'part.wav'}]})
    write(game/'project.json', {'schema_version':2,'title':'Audio graph test','dimension':'2d',
                               'script':'main.luau','entities':[], 'music':'theme',
                               'assets':{'theme':{'type':'music','path':'music.json'}}})
    (game/'main.luau').write_text('return {}\n', encoding='utf-8')
    run('validate','--project',game)
    revision = run('inspect','--project',game)['revision']
    run('checkpoint','--project',game,'--name','original')
    pcm(8192)
    assert run('inspect','--project',game)['revision'] != revision
    run('restore','--project',game,'--name','original')
    assert (game/'part.wav').read_bytes() == original
    assert run('inspect','--project',game)['revision'] == revision
    for target in ['windows','android','ios']:
        cooked = work/('cooked-'+target)
        run('cook','--project',game,'--target',target,'--output',cooked)
        assert (cooked/'part.wav').read_bytes() == original
        assert json.loads((cooked/'cook-manifest.json').read_text(encoding='utf-8'))['audio_feature_revision'] == 1
        run('validate','--project',cooked)
    run('asset','render','--project',game,'--id','theme','--output',game/'part.wav',ok=False)
    assert (game/'part.wav').read_bytes() == original
    run('music','import','--source',game/'part.wav','--project',game,'--output-dir',game/'imported',
        '--frames','4800','--license','Original synthetic test fixture')
    assert (game/'imported/audition.wav').is_file()
    source = ROOT/'examples/music/lanterns.composition.json'
    doc = json.loads(source.read_text(encoding='utf-8'))
    output = work/'transformed.json'
    run('music','transform','--score',source,'--track',doc['tracks'][0]['id'],
        '--velocity','0.8','--output',output)
    transformed = json.loads(output.read_text(encoding='utf-8'))
    assert transformed['tracks'][1:] == doc['tracks'][1:]
    run('music','validate','--score',output)
    report = {'ok':True,'work':str(work),'checks':['recorded source revision','binary checkpoint restore',
              'Windows/Android/iOS complete source cook','audio feature requirement',
              'authored WAV overwrite protection','recorded import and native audition',
              'targeted composition transform preserves other parts'],
              'engine_sha256':hashlib.sha256(engine.read_bytes()).hexdigest()}
    write(work/'report.json', report)
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
