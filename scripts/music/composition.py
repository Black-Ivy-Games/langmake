"""Strict, dependency-free symbolic score compilation. Beat 0 is the start."""
import copy
import math
import random
import re


def obj(value, keys, path):
    if not isinstance(value, dict):
        raise ValueError(f'{path}: expected object')
    extra = set(value) - set(keys.split())
    if extra:
        raise ValueError(f'{path}: unknown fields {sorted(extra)}')


def number(value, lo, hi, path):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not lo <= value <= hi:
        raise ValueError(f'{path}: expected number in {lo}..{hi}')
    return value


def ident(value):
    if not isinstance(value, str) or not re.fullmatch(r'[A-Za-z0-9_.-]{1,64}', value):
        raise ValueError(f'invalid ID: {value!r}')
    return value


def pitch(value):
    if isinstance(value, int) and not isinstance(value, bool) and 0 <= value <= 127:
        return value
    m = re.fullmatch(r'([A-Ga-g])([#b]?)(-1|[0-9])', str(value))
    if not m:
        raise ValueError(f'pitch must be MIDI 0..127 or scientific pitch such as C4: {value!r}')
    result = (int(m[3]) + 1) * 12 + dict(C=0, D=2, E=4, F=5, G=7, A=9, B=11)[m[1].upper()] + {'': 0, '#': 1, 'b': -1}[m[2]]
    return int(number(result, 0, 127, 'pitch'))


class Composition:
    def __init__(self, doc, instruments):
        obj(doc, 'schema_version type title length_beats beats_per_bar tempo tracks patterns sections seed humanize loop tail_seconds effects gain', 'score')
        if doc.get('schema_version') != 1 or doc.get('type') != 'composition':
            raise ValueError('Expected composition schema_version 1')
        self.doc = copy.deepcopy(doc)
        if not isinstance(doc.get('title', 'Original composition'), str) or not 1 <= len(doc.get('title', 'Original composition')) <= 128:
            raise ValueError('title must be 1..128 characters')
        self.length = number(doc.get('length_beats'), .125, 4096, 'length_beats')
        self.meter = number(doc.get('beats_per_bar', 4), 1, 16, 'beats_per_bar')
        if not isinstance(self.meter, int):
            raise ValueError('beats_per_bar must be an integer')
        if not isinstance(doc.get('loop', True), bool):
            raise ValueError('loop must be boolean')
        number(doc.get('gain', .7), 0, 1, 'gain')
        self.tempo = doc.get('tempo', [{'beat': 0, 'bpm': 120}])
        if not isinstance(self.tempo, list) or not 1 <= len(self.tempo) <= 256:
            raise ValueError('tempo requires 1..256 points')
        for i, t in enumerate(self.tempo):
            obj(t, 'beat bpm', 'tempo')
            number(t.get('beat'), 0, self.length, 'tempo beat')
            number(t.get('bpm'), 20, 400, 'tempo bpm')
            if (i == 0 and t['beat'] != 0) or (i and t['beat'] <= self.tempo[i-1]['beat']):
                raise ValueError('tempo starts at beat 0 and must increase')
        self.duration = self.seconds(self.length)
        self.tail = number(doc.get('tail_seconds', 2), 0, 10, 'tail_seconds')
        if self.duration + self.tail > 180:
            raise ValueError('Composition render limit is 180 seconds including release tail; author longer music as cues')
        self.sections = doc.get('sections', {})
        if not isinstance(self.sections, dict) or len(self.sections) > 128:
            raise ValueError('sections must be an object with at most 128 named ranges')
        for name, section in self.sections.items():
            ident(name); obj(section, 'start end', 'section')
            number(section.get('start'), 0, self.length, 'section start')
            number(section.get('end'), section['start']+.001, self.length, 'section end')
        patterns = doc.get('patterns', {})
        if not isinstance(patterns, dict) or len(patterns) > 256:
            raise ValueError('patterns supports at most 256 named patterns')
        for name, p in patterns.items():
            ident(name); obj(p, 'length_beats notes', 'pattern')
            number(p.get('length_beats'), .125, self.length, 'pattern length')
            self.notes(p.get('notes'), p['length_beats'])
        human = doc.get('humanize', {})
        obj(human, 'timing_ms velocity', 'humanize')
        timing = number(human.get('timing_ms', 0), 0, 30, 'timing_ms') / 1000
        velocity = number(human.get('velocity', 0), 0, .15, 'velocity humanization')
        seed = number(doc.get('seed', 1), 0, 2147483647, 'seed')
        if not isinstance(seed, int):
            raise ValueError('seed must be an integer')
        tracks = doc.get('tracks')
        if not isinstance(tracks, list) or not 1 <= len(tracks) <= 16:
            raise ValueError('tracks requires 1..16 entries')
        self.tracks = []; ids = set(); total = 0
        for track in tracks:
            obj(track, 'id instrument articulations notes arrangement gain pan expression effects', 'track')
            id = ident(track.get('id'))
            if id in ids:
                raise ValueError('duplicate track ID: '+id)
            ids.add(id)
            # Per-track RNG keeps unrelated tracks stable during targeted edits.
            rng = random.Random(str(seed)+':'+id)
            instrument = track.get('instrument')
            if instrument not in instruments:
                raise ValueError(f'{id}: unknown instrument {instrument!r}; run music instruments')
            arts = track.get('articulations', {})
            if not isinstance(arts, dict) or len(arts) > 8:
                raise ValueError('at most 8 articulation mappings per track')
            for name, target in arts.items():
                ident(name)
                if target not in instruments:
                    raise ValueError('unknown articulation instrument: '+str(target))
            notes = self.notes(track.get('notes', []), self.length)
            arr = track.get('arrangement', [])
            if not isinstance(arr, list) or len(arr) > 1024:
                raise ValueError('arrangement allows 1024 entries per track')
            for entry in arr:
                obj(entry, 'pattern beat repeat transpose velocity', 'arrangement')
                if entry.get('pattern') not in patterns:
                    raise ValueError('unknown pattern')
                p = patterns[entry['pattern']]
                offset = number(entry.get('beat'), 0, self.length, 'arrangement beat')
                repeat = number(entry.get('repeat', 1), 1, 128, 'repeat')
                transpose = number(entry.get('transpose', 0), -127, 127, 'transpose')
                if not isinstance(repeat, int) or not isinstance(transpose, int):
                    raise ValueError('repeat and transpose must be integers')
                scale = number(entry.get('velocity', 1), 0, 2, 'arrangement velocity')
                if offset + p['length_beats']*repeat > self.length:
                    raise ValueError('pattern extends beyond composition')
                for r in range(repeat):
                    for n in self.notes(p['notes'], p['length_beats']):
                        n['beat'] += offset+r*p['length_beats']; n['pitch'] += transpose; n['velocity'] = min(1, n['velocity']*scale)
                        notes.append(n)
                        if len(notes) > 65536:
                            raise ValueError('expanded note limit is 65536')
            compiled = []
            for n in notes:
                inst = instrument if not n.get('articulation') else arts.get(n['articulation'])
                if inst not in instruments:
                    raise ValueError(f'{id}: unsupported articulation {n.get("articulation")}')
                info = instruments[inst]
                if not info['low'] <= n['pitch'] <= info['high']:
                    raise ValueError(f'{id}: pitch {n["pitch"]} outside {inst} range {info["low"]}..{info["high"]}')
                start = max(0, self.seconds(n['beat'])+rng.uniform(-timing, timing))
                end = max(start+1/48000, min(self.duration, self.seconds(n['beat']+n['duration'])+rng.uniform(-timing, timing)))
                compiled.append({**n, 'source_velocity': n['velocity'], 'instrument': inst, 'start': round(start*48000), 'end': round(end*48000), 'velocity': max(1, min(127, round(127*(n['velocity']+rng.uniform(-velocity, velocity)))))})
            compiled.sort(key=lambda n: (n['start'], n['pitch']))
            # Include release envelopes when bounding the sampler voice budget;
            # otherwise dense repetition could silently steal older notes.
            voice_events = {}
            for note in compiled:
                events = voice_events.setdefault(note['instrument'], [])
                events.extend([(note['start'], 1), (note['end'] + round(instruments[note['instrument']]['release']*48000), -1)])
            for events in voice_events.values():
                active = 0
                for _, change in sorted(events):
                    active += change
                    if active > 128:
                        raise ValueError(f'{id}: exceeds 128 simultaneous sampler voices including release tails; reduce note density')
            ends = {}
            for n in compiled:
                key = (n['instrument'], n['pitch'])
                if n['start'] < ends.get(key, -1):
                    raise ValueError(f'{id}: overlapping same-pitch notes after humanization at beat {n["beat"]}; shorten the note or reduce humanization')
                ends[key] = n['end']
            expr = track.get('expression', [])
            if not isinstance(expr, list) or len(expr) > 4096:
                raise ValueError('expression allows at most 4096 points per track')
            for i, point in enumerate(expr):
                obj(point, 'beat value', 'expression');number(point.get('beat'), 0, self.length, 'expression beat');number(point.get('value'), 0, 1, 'expression value')
                if i and point['beat'] <= expr[i-1]['beat']:
                    raise ValueError('expression beats must increase')
            total += len(compiled)
            if total > 65536:
                raise ValueError('expanded composition exceeds 65536 notes')
            self.tracks.append({**track, 'notes': compiled, 'gain': number(track.get('gain', 1), 0, 2, 'track gain'), 'pan': number(track.get('pan', 0), -1, 1, 'track pan')})

    @staticmethod
    def notes(notes, length):
        if not isinstance(notes, list) or len(notes) > 65536:
            raise ValueError('notes must be an array of at most 65536 notes')
        out = []
        for n in notes:
            obj(n, 'beat duration pitch velocity articulation', 'note')
            beat = number(n.get('beat'), 0, length, 'note beat')
            dur = number(n.get('duration'), 1/960, length, 'note duration')
            if beat+dur > length+1e-9:
                raise ValueError('note extends beyond score or pattern')
            out.append({**n, 'beat': beat, 'duration': dur, 'pitch': pitch(n.get('pitch')), 'velocity': number(n.get('velocity', .7), .001, 1, 'note velocity')})
        return out

    def seconds(self, beat):
        total = 0
        for i, t in enumerate(self.tempo):
            end = min(beat, self.tempo[i+1]['beat'] if i+1 < len(self.tempo) else beat)
            if end > t['beat']:
                total += (end-t['beat'])*60/t['bpm']
            if end == beat:
                break
        return total

    def summary(self):
        return {'ok': True, 'duration_seconds': self.duration, 'notes': sum(len(t['notes']) for t in self.tracks), 'tracks': [{'id': t['id'], 'instrument': t['instrument'], 'notes': len(t['notes'])} for t in self.tracks], 'sections': self.sections, 'render_limit_seconds': 180}


def schema():
    n = lambda lo, hi: {'type': 'number', 'minimum': lo, 'maximum': hi}
    shape = lambda p, r=[]: {'type': 'object', 'additionalProperties': False, 'properties': p, 'required': r}
    array = lambda item, limit: {'type': 'array', 'items': item, 'maxItems': limit}
    id = {'type': 'string', 'pattern': '^[A-Za-z0-9_.-]{1,64}$'}
    note = shape({'beat': n(0,4096), 'duration': n(1/960,4096), 'pitch': {'oneOf': [{'type':'integer','minimum':0,'maximum':127}, {'type':'string','pattern':'^[A-Ga-g][#b]?(-1|[0-9])$'}]}, 'velocity': n(.001,1), 'articulation': id}, ['beat','duration','pitch'])
    track = shape({'id':id,'instrument':id,'articulations':{'type':'object','maxProperties':8,'additionalProperties':id}, 'notes':array(note,65536), 'arrangement':array(shape({'pattern':id,'beat':n(0,4096),'repeat':{'type':'integer','minimum':1,'maximum':128},'transpose':{'type':'integer','minimum':-127,'maximum':127},'velocity':n(0,2)},['pattern','beat']),1024), 'gain':n(0,2),'pan':n(-1,1),'expression':array(shape({'beat':n(0,4096),'value':n(0,1)},['beat','value']),4096),'effects':{'type':'array','maxItems':4}}, ['id','instrument'])
    return {'$schema':'https://json-schema.org/draft/2020-12/schema', **shape({'schema_version':{'const':1},'type':{'const':'composition'},'title':{'type':'string','minLength':1,'maxLength':128},'length_beats':n(.125,4096),'beats_per_bar':{'type':'integer','minimum':1,'maximum':16},'tempo':array(shape({'beat':n(0,4096),'bpm':n(20,400)},['beat','bpm']),256),'tracks':{**array(track,16),'minItems':1},'patterns':{'type':'object','maxProperties':256,'additionalProperties':shape({'length_beats':n(.125,4096),'notes':array(note,65536)},['length_beats','notes'])},'sections':{'type':'object','maxProperties':128,'additionalProperties':shape({'start':n(0,4096),'end':n(0,4096)},['start','end'])},'seed':{'type':'integer','minimum':0,'maximum':2147483647},'humanize':shape({'timing_ms':n(0,30),'velocity':n(0,.15)}),'loop':{'type':'boolean'},'tail_seconds':n(0,10),'gain':n(0,1),'effects':{'type':'array','maxItems':4}},['schema_version','type','length_beats','tracks'])}
