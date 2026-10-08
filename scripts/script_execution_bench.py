"""Isolated engine binding/guard measurements. Never runs a consumer or device."""
import argparse
import json
import statistics
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def run(engine, directory, command, steps, bindings=False):
    args = [str(engine), command, '--project', str(directory), '--steps', str(steps),
            '--input', str(directory / 'input.json')]
    if bindings:
        args.append('--script-bindings')
    completed = subprocess.run(args, cwd=ROOT, capture_output=True, text=True, encoding='utf-8', timeout=90)
    result = json.loads(completed.stdout)
    if completed.returncode or not result.get('ok'):
        raise RuntimeError(json.dumps(result))
    return result['profile']


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--engine', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if ROOT not in output.parents or output.exists():
        raise ValueError('Use a new output file inside the engine workspace')
    fixtures = output.parent / 'binding-fixtures'
    fixtures.mkdir(parents=True, exist_ok=False)
    evidence = {'steps': 180, 'repetitions': 3, 'cases': {}}
    for name, keys, text_size, values, events in [('small', 4, 16, 16, 8), ('large', 48, 4096, 1024, 128)]:
        directory = fixtures / name
        directory.mkdir()
        data = {'prefill' + str(i): 'x' * text_size for i in range(keys)}
        manifest = {'schema_version': 2, 'project_id': 'engine-binding-' + name, 'dimension': '2d',
                    'title': 'Isolated engine binding fixture', 'script': 'fixture.luau', 'entities': [],
                    'gravity': [0, 0, 0], 'state': data, 'ui': {'data': data}}
        (directory / 'project.json').write_text(json.dumps(manifest), encoding='utf-8')
        script = ('local value=table.create(%d,42)\nreturn {update=function()\n'
                  'local events=engine.input_events()\nfor i=1,4 do\n'
                  "engine.ui_set('value'..i,value)\nengine.state_set('value'..i,value)\nend\nend}\n") % values
        (directory / 'fixture.luau').write_text(script, encoding='utf-8')
        timeline = {'events': [{'from': 0, 'to': 180, 'events': [{'type': 'fixture',
                    'items': [{'id': i, 'label': 'synthetic fixture'} for i in range(events)]}]}]}
        (directory / 'input.json').write_text(json.dumps(timeline), encoding='utf-8')
        case = {'fixture': {'prefilled_keys': keys, 'text_bytes_per_key': text_size,
                            'value_items': values, 'event_items': events,
                            'ui_writes_per_hook': 4, 'state_writes_per_hook': 4}}
        for label, engine in [('before', args.baseline.resolve()), ('after', args.engine.resolve())]:
            runs = [run(engine, directory, 'simulate', 180) for _ in range(3)]
            case[label] = {'p50_ms': statistics.median(x['step_p50_ms'] for x in runs),
                           'p95_ms': statistics.median(x['step_p95_ms'] for x in runs),
                           'max_ms': max(x['step_max_ms'] for x in runs)}
        measured = run(args.engine.resolve(), directory, 'profile', 180, True)
        case['profiled'] = {'p50_ms': measured['step_p50_ms'], 'p95_ms': measured['step_p95_ms'],
                            'last_hook': measured['runtime']['script_execution']}
        case['guard_overhead_percent'] = (case['after']['p50_ms'] / case['before']['p50_ms'] - 1) * 100
        case['profiling_overhead_percent'] = (case['profiled']['p50_ms'] / case['after']['p50_ms'] - 1) * 100
        evidence['cases'][name] = case
    output.write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(evidence, indent=2))


if __name__ == '__main__':
    main()
