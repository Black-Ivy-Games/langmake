import copy
import json
from pathlib import Path
import tempfile
import unittest
from composition import Composition, pitch, schema
from sampler import sfz
from tooling import pack_manifest

ROOT=Path(__file__).resolve().parents[2]
INSTRUMENTS=pack_manifest()['instruments']


class CompositionTests(unittest.TestCase):
    def score(self):
        return {'schema_version':1,'type':'composition','length_beats':8,'tempo':[{'beat':0,'bpm':120},{'beat':4,'bpm':60}], 'tracks':[{'id':'keys','instrument':'piano','notes':[{'beat':3,'duration':2,'pitch':'C4','velocity':.7}]}]}

    def test_tempo_and_pitch(self):
        c=Composition(self.score(),INSTRUMENTS)
        self.assertEqual(c.seconds(3),1.5);self.assertEqual(c.seconds(5),3)
        n=c.tracks[0]['notes'][0];self.assertEqual((n['start'],n['end'],n['pitch']),(72000,144000,60))
        self.assertEqual(pitch('Db4'),61)
        for p in ['C10',128,-2,True,'z3']:
            with self.assertRaises(ValueError):pitch(p)

    def test_rejects(self):
        for change in [lambda d:d.update(nope=True),lambda d:d.update(tempo=[{'beat':1,'bpm':120}]),lambda d:d['tracks'][0].update(instrument='invented'),lambda d:d['tracks'][0]['notes'][0].update(pitch=12),lambda d:d['tracks'][0]['notes'][0].update(articulation='legato'),lambda d:d.update(length_beats=4096),lambda d:d['tracks'][0].update(expression=[{'beat':2,'value':.5},{'beat':1,'value':.5}])]:
            d=self.score();change(d)
            with self.assertRaises(ValueError):Composition(d,INSTRUMENTS)

    def test_patterns_humanization_and_isolation(self):
        d=self.score();d['tracks'][0]['notes']=[]
        d['patterns']={'p':{'length_beats':2,'notes':[{'beat':0,'duration':1,'pitch':60}]}}
        d['tracks'][0]['arrangement']=[{'pattern':'p','beat':0,'repeat':4,'transpose':2}]
        d['humanize']={'timing_ms':10,'velocity':.03};d['seed']=42
        c=Composition(d,INSTRUMENTS);self.assertEqual(len(c.tracks[0]['notes']),4)
        self.assertEqual(c.tracks,Composition(d,INSTRUMENTS).tracks)
        other=copy.deepcopy(d['tracks'][0]);other['id']='new';d['tracks'].insert(0,other)
        self.assertEqual(c.tracks[0],Composition(d,INSTRUMENTS).tracks[1])

    def test_pack_and_examples(self):
        p=pack_manifest();self.assertEqual(p['license'],'CC0-1.0');self.assertEqual(len(p['instruments']),15)
        for info in p['instruments'].values():
            text=sfz(info);self.assertIn('<region>',text);self.assertIn('amplitude_oncc11',text)
            self.assertEqual(len({r['path'] for r in info['regions']}),len(info['regions']))
        for path in (ROOT/'examples/music').glob('*.composition.json'):
            Composition(json.loads(path.read_text()),INSTRUMENTS)
        self.assertFalse(schema()['additionalProperties'])

    def test_voice_budget_includes_releases(self):
        d=self.score()
        d['tracks'][0]['notes']=[{'beat':r*.005,'duration':.0011,'pitch':p} for r in range(3) for p in range(36,89)]
        with self.assertRaisesRegex(ValueError,'128 simultaneous sampler voices'):
            Composition(d,INSTRUMENTS)


if __name__=='__main__':unittest.main()
