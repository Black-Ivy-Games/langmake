"""Offline sfizz adapter. No sampler, Python, or bank is needed by the player."""
import array
import ctypes as C
import hashlib
import json
import math
import os
from pathlib import Path
import sys
import wave


def library(root):
    configured = os.environ.get('LANGMAKE_SFIZZ')
    names = ['libsfizz.dll', 'libsfizz.dylib', 'libsfizz.so', 'libsfizz.so.1']
    candidates = [Path(configured)] if configured else [root / folder / n for folder in ['music-tools/bin', '.deps/music/bin'] for n in names]
    path = next((p for p in candidates if p.is_file()), None)
    if not path:
        raise ValueError('sfizz authoring library is missing. Run langmake music setup (Git, CMake and a C++ compiler required); or set LANGMAKE_SFIZZ to a trusted built library. Games do not need it.')
    dll_dir = os.add_dll_directory(str(path.resolve().parent)) if sys.platform == 'win32' else None
    try:
        lib = C.CDLL(str(path.resolve()))
    except OSError as e:
        raise ValueError(f'Cannot load sfizz authoring library {path}: {e}') from e
    lib._dll_dir = dll_dir
    signatures = {
        'sfizz_create_synth': (C.c_void_p, []), 'sfizz_free': (None, [C.c_void_p]),
        'sfizz_set_sample_rate': (None, [C.c_void_p, C.c_float]),
        'sfizz_set_samples_per_block': (None, [C.c_void_p, C.c_int]),
        'sfizz_set_num_voices': (None, [C.c_void_p, C.c_int]),
        'sfizz_set_volume': (None, [C.c_void_p, C.c_float]),
        'sfizz_enable_freewheeling': (None, [C.c_void_p]),
        'sfizz_load_string': (C.c_bool, [C.c_void_p, C.c_char_p, C.c_char_p]),
        'sfizz_send_note_on': (None, [C.c_void_p, C.c_int, C.c_int, C.c_int]),
        'sfizz_send_note_off': (None, [C.c_void_p, C.c_int, C.c_int, C.c_int]),
        'sfizz_send_cc': (None, [C.c_void_p, C.c_int, C.c_int, C.c_int]),
        'sfizz_render_block': (None, [C.c_void_p, C.POINTER(C.POINTER(C.c_float)), C.c_int, C.c_int]),
        'sfizz_get_num_regions': (C.c_int, [C.c_void_p]),
    }
    for name, (result, args) in signatures.items():
        f = getattr(lib, name); f.restype = result; f.argtypes = args
    return lib, str(path.resolve())


def sfz(info):
    # No undocumented bank scripts or arbitrary opcode injection. The curated
    # map deliberately uses a small, auditable subset and first round-robin.
    lines = [f'<global> ampeg_attack=0.003 ampeg_release={info["release"]} volume=-6 amplitude_oncc11=100 amplitude_smoothcc11=20', '<control> set_cc11=127']
    velocities = sorted(set(r['velocity_layer'] for r in info['regions']))
    for vi, vel in enumerate(velocities):
        regions = sorted((r for r in info['regions'] if r['velocity_layer'] == vel), key=lambda r: r['key'])
        for i, r in enumerate(regions):
            low = info['low'] if i == 0 else (regions[i-1]['key'] + r['key'])//2+1
            high = info['high'] if i+1 == len(regions) else (regions[i+1]['key'] + r['key'])//2
            if high < low:
                continue
            lines.append(f'<region> sample={r["path"]} pitch_keycenter={r["key"]} lokey={low} hikey={high} lovel={1+vi*127//len(velocities)} hivel={(vi+1)*127//len(velocities)} loop_mode=no_loop')
    return '\n'.join(lines)


def render_track(lib, pack, track, composition, frames):
    instances = {}
    block = 512
    buffers = [(C.c_float * block)(), (C.c_float * block)()]
    channels = (C.POINTER(C.c_float)*2)(*buffers)
    output = array.array('f', [0]) * (frames*2)
    try:
        for instrument in sorted(set(n['instrument'] for n in track['notes'])):
            synth = lib.sfizz_create_synth()
            if not synth:
                raise ValueError('sfizz allocation failed')
            instances[instrument] = [synth, [], 0]
            lib.sfizz_set_sample_rate(synth, 48000); lib.sfizz_set_samples_per_block(synth, block)
            lib.sfizz_set_num_voices(synth, 128); lib.sfizz_set_volume(synth, 0)
            lib.sfizz_enable_freewheeling(synth)
            description = sfz(pack['instruments'][instrument])
            base = str(pack['_directory'] / 'instrument.sfz').encode('utf-8')
            if not lib.sfizz_load_string(synth, base, description.encode('utf-8')) or not lib.sfizz_get_num_regions(synth):
                raise ValueError('sfizz could not load mapped instrument: '+instrument)
        for n in track['notes']:
            events = instances[n['instrument']][1]
            events.extend([(n['start'], 2, n['pitch'], n['velocity']), (n['end'], 0, n['pitch'], 0)])
        # Expression is linearly sampled every 10 ms, then smoothed in SFZ. It
        # affects held notes as well as new notes, unlike MIDI velocity alone.
        expression = track.get('expression', [])
        if expression:
            points = [(round(composition.seconds(p['beat'])*48000), p['value']) for p in expression]
            if points[0][0] > 0:
                points.insert(0, (0,1))
            control = []; previous = None; segment = 0
            for frame in range(0,frames,480):
                while segment+1 < len(points) and points[segment+1][0] <= frame:
                    segment += 1
                a = points[segment]; value = a[1]
                if segment+1 < len(points):
                    b = points[segment+1]; value += (b[1]-a[1])*(frame-a[0])/(b[0]-a[0])
                value = max(0,min(127,round(value*127)))
                if value != previous:
                    control.append((frame,1,11,value)); previous = value
            for _, events, _ in instances.values():
                events.extend(control)
        for value in instances.values():
            value[1].sort()
        for start in range(0, frames, block):
            count = min(block, frames-start)
            for value in instances.values():
                synth, events, index = value
                while index < len(events) and events[index][0] < start+count:
                    frame, kind, key, vel = events[index]
                    (lib.sfizz_send_note_off if kind == 0 else lib.sfizz_send_cc if kind == 1 else lib.sfizz_send_note_on)(synth, max(0,frame-start), key, vel)
                    index += 1
                value[2] = index
                lib.sfizz_render_block(synth, channels, 2, count)
                for i in range(count):
                    output[(start+i)*2] += buffers[0][i]; output[(start+i)*2+1] += buffers[1][i]
        if any(not math.isfinite(x) for x in output):
            raise ValueError('Sampler produced nonfinite PCM')
        return output
    finally:
        for synth, _, _ in instances.values():
            lib.sfizz_free(synth)


def write_wav(path, pcm, gain=1):
    path = Path(path); path.parent.mkdir(parents=True, exist_ok=True)
    peak = max((abs(x*gain) for x in pcm), default=0)
    clipped = sum(abs(x*gain)>1 for x in pcm)
    raw = array.array('h', (round(max(-1,min(1,x*gain))*32767) for x in pcm))
    if sys.byteorder != 'little':
        raw.byteswap()
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(48000); w.writeframes(raw.tobytes())
    return {'path': str(path.resolve()), 'sample_frames': len(pcm)//2, 'peak': peak, 'clipped_samples': clipped, 'rms': math.sqrt(sum((x*gain)**2 for x in pcm)/max(1,len(pcm))), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def render(root, composition, pack, output, project, section=None, track_id=None):
    lib, libpath = library(root)
    output = Path(output).resolve(); project = Path(project).resolve()
    if not output.is_relative_to(project):
        raise ValueError('output directory must be inside --project; native audio references are project-relative')
    if output.exists() and any(output.iterdir()):
        raise ValueError('output directory must be empty; render each revision into its own directory')
    output.mkdir(parents=True, exist_ok=True)
    if section and section not in composition.sections:
        raise ValueError('unknown section: '+section)
    if track_id and track_id not in [t['id'] for t in composition.tracks]:
        raise ValueError('unknown track: '+track_id)
    selection = composition.sections.get(section, {'start':0,'end':composition.length})
    first = round(composition.seconds(selection['start'])*48000)
    end = round(composition.seconds(selection['end'])*48000)
    musical_frames = round(composition.duration*48000)
    full_frames = musical_frames+round(composition.tail*48000)
    looping = composition.doc.get('loop',True) and not section
    recipe = {'schema_version':2,'type':'music','title':composition.doc.get('title','Original composition'),'bpm':composition.tempo[0]['bpm'],'gain':composition.doc.get('gain',.7),'beats_per_bar':composition.meter,'length_frames':end-first if looping or section else full_frames,'tracks':[], 'effects':composition.doc.get('effects',[]), 'tail_seconds':0}
    if recipe['length_frames']*4+44>32*1024*1024:
        raise ValueError('Each baked WAV must fit the 32 MiB source-file budget; shorten the cue')
    selected_count = 1 if track_id else len(composition.tracks)
    if recipe['length_frames']*4*selected_count>96*1024*1024:
        raise ValueError('Selected synchronized stems would exceed native 96 MiB asset budget; shorten cue, render fewer tracks, or mix compatible parts')
    # Explicit beat grid preserves tempo changes for runtime layer transitions.
    recipe['beat_frames'] = [round(composition.seconds(b)*48000)-first for b in range(math.ceil(selection['start']), math.ceil(selection['end'])) if round(composition.seconds(b)*48000)>=first]
    if not recipe['beat_frames'] or recipe['beat_frames'][0] != 0:
        recipe['beat_frames'].insert(0,0)
    reports = []; rendered = []
    for track in composition.tracks:
        if track_id and track['id'] != track_id:
            continue
        pcm = render_track(lib,pack,track,composition,full_frames)
        if looping:
            # Fold release tails into the next iteration, preserving alignment.
            for i in range(musical_frames*2,len(pcm)):
                pcm[i%(musical_frames*2)] += pcm[i]
            del pcm[musical_frames*2:]
        if section:
            pcm = pcm[first*2:end*2]
        peak = max((abs(x) for x in pcm),default=0)
        rendered.append((track,pcm,peak))
    # Preserve relative performance dynamics while using PCM headroom. A single
    # scale for all stems avoids making a quiet part as loud as the lead.
    highest = max((peak for _,_,peak in rendered),default=0)
    scale = min(64,.8/max(.00001,highest))
    for track,pcm,peak in rendered:
        report = write_wav(output/(track['id']+'.wav'),pcm,scale); report['baked_scale'] = scale
        reports.append(report)
        entry = {'id':track['id'],'source':(output/(track['id']+'.wav')).relative_to(project).as_posix(),'gain':track['gain'],'pan':track['pan'],'effects':track.get('effects',[])}
        recipe['tracks'].append(entry)
    source = json.dumps(composition.doc,sort_keys=True).encode()
    recipe['provenance'] = {'tool':'Langmake music authoring','sampler':'sfizz 1.2.3','composition_sha256':hashlib.sha256(source).hexdigest(),'pack':pack['id'],'pack_commit':pack['commit'],'instrument_license':pack['license'],'section':section,'loop_tail_folded':looping}
    (output/'music.json').write_text(json.dumps(recipe,indent=2)+'\n',encoding='utf-8')
    (output/'composition.json').write_text(json.dumps(composition.doc,indent=2)+'\n',encoding='utf-8')
    (output/'LICENSE-instruments.txt').write_text((pack['_directory']/'LICENSE').read_text(encoding='utf-8'),encoding='utf-8')
    report = {'ok':True,'recipe':str(output/'music.json'),'sampler_library':libpath,'tracks':reports,'decoded_bytes':sum(x['sample_frames']*4 for x in reports),'provenance':recipe['provenance'],'musical_approval':'awaiting human audition'}
    (output/'render-report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return report
