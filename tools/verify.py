"""Run maintained regressions using curated public baselines, never local archives."""
from __future__ import annotations
import argparse
import csv
import gzip
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/validation'
ENV = dict(os.environ, PYTHONPATH=str(ROOT / 'API/python/src'),
           ABCURVES_LIBRARY=str(ROOT / 'build/api/Release/abcurves.dll'),
           ABCURVES_LIB_DIR=str(ROOT / 'build/api/Release'),
           CARGO_TARGET_DIR=str(ROOT / 'build/rust'))
ENV['PATH'] = str(ROOT / 'build/api/Release') + os.pathsep + ENV['PATH']
records = []


def run(name, command, cwd=ROOT, extra=None):
    OUT.mkdir(parents=True, exist_ok=True)
    if '--resume' in sys.argv and any(r['name'] == name and r['exit_code'] == 0 for r in records):
        print(name + ' (retained successful check)', flush=True)
        return
    print(name, flush=True)
    start = time.perf_counter()
    with (OUT / (name + '.log')).open('w', encoding='utf-8') as log:
        result = subprocess.run(list(map(str, command)), cwd=cwd, env={**ENV, **(extra or {})},
                                stdout=log, stderr=subprocess.STDOUT)
    records.append(dict(name=name, exit_code=result.returncode, seconds=time.perf_counter()-start,
                        command=list(map(str, command))))
    (OUT / 'commands.json').write_text(json.dumps(records, indent=2), encoding='utf-8')
    if result.returncode:
        raise RuntimeError(f'{name} failed; see build/validation/{name}.log')


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / ('API/tools/' + name + '.py'))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def rows(path):
    def number(value):
        try:
            return int(value) if len(value) < 22 else float(value)
        except ValueError:
            try:
                return float(value)
            except ValueError:
                return value
    opener = gzip.open if path.suffix == '.gz' else open
    with opener(path, 'rt', newline='') as stream:
        return [[number(x) for x in row] for row in csv.reader(stream)]


def differential():
    baseline = ROOT / 'tests/baselines'
    # Reuse the maintained replay implementations. Input fixtures are immutable.
    m = module('validate_step3')
    m.OUT = OUT / 'observations'
    m.fixtures = lambda: ((int(p.parent.name.split('ms')[0]), p.parent.name, p.read_text())
                          for p in sorted((baseline / 'observations').glob('*/input.fixture')))
    m.main()
    for old in (baseline / 'observations').glob('*/direct.csv.gz'):
        assert rows(old) == rows(m.OUT / old.parent.name / 'direct.csv'), old
    m = module('validate_step4')
    m.OUT = OUT / 'segments'
    m.fixtures = lambda: ((int(p.stem.split('ms')[0]), p.stem, p)
                          for p in sorted((ROOT / 'API/tests/step4/fixtures').glob('*.fixture')))
    m.main()
    for old in (baseline / 'segments').glob('*/direct.csv.gz'):
        assert rows(old) == rows(m.OUT / old.parent.name / 'direct.csv'), old
    for name, folder in [('validate_step4_1', 'recovery'), ('validate_step4_2', 'hardening_replay')]:
        m = module(name)
        m.OUT = OUT if folder == 'hardening_replay' else OUT / folder
        m.main()
        for old in (baseline / folder).glob('*_direct.csv.gz'):
            assert rows(old) == rows(OUT / folder / old.name[:-3]), old
        for fixture in (baseline / folder).glob('*.fixture'):
            assert fixture.read_bytes() == (OUT / folder / fixture.name).read_bytes(), fixture
    print('All retained differential records are unchanged.', flush=True)


def regression():
    for config in ('Release', 'Debug'):
        run('build_' + config, ['cmake', '--build', 'build/api', '--config', config, '--parallel', '4'])
        run('ctest_' + config, ['ctest', '--test-dir', 'build/api', '-C', config, '--output-on-failure'])
    run('python', [sys.executable, '-m', 'pytest', 'API/python/tests', 'API/tools/tests', '-q'])
    run('api_inventory', [sys.executable, 'API/tools/audit_application_io.py', '--probes'])
    run('rust', ['cargo', 'test', '--manifest-path', 'API/rust/Cargo.toml', '--release'])
    run('rust_examples', ['cargo', 'build', '--manifest-path', 'API/rust/Cargo.toml', '--release', '--examples'])
    run('models', [sys.executable, '-c',
        "import importlib.util;from pathlib import Path;s=importlib.util.spec_from_file_location('v','lib/tests/differential/model_validation.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m);m.EXE=Path('build/api/engine/Release/abcurves_numerical_probe.exe').resolve();m.RESULT=Path('build/validation/models').resolve();m.main()"])
    run('components', [sys.executable, 'lib/tests/differential/qualify.py', '--build', 'build/api/engine/Release', '--output', 'build/validation/components'])
    run('legacy', [sys.executable, 'API/benchmarks/run_cross_language.py', '--full', '--results', OUT / 'legacy'])
    run('differential', [sys.executable, __file__, '--differential'])


def asan():
    run('asan_configure', ['cmake', '-S', '.', '-B', 'build/asan', '-DABC_ENABLE_ASAN=ON'])
    targets = ['abc_c_contract', 'abc_cpp_contract', 'abc_c_renderer', 'abc_c_observation',
               'abcurves_observation_contract', 'abc_c_segments', 'abcurves_segment_contract',
               'abc_c_recovery', 'abcurves_segment_recovery', 'abc_api_hardening', 'abc_c_hardening']
    run('asan_build', ['cmake', '--build', 'build/asan', '--config', 'RelWithDebInfo', '--target', *targets, '--parallel', '4'])
    runtime = sorted(Path('C:/Program Files/Microsoft Visual Studio').glob('*/Community/VC/Tools/MSVC/*/bin/Hostx64/x64'))[-1]
    pattern = '^abc_(c_contract_(32|16|8)|cpp_contract|c_renderer|c_observation_(32|16|8)|c_segments_(32|16|8)|c_recovery_(32|16|8)|api_hardening|c_hardening_(32|16|8))$|^abcurves_(observation_contract|segment_contract|segment_recovery)$'
    run('asan_tests', ['ctest', '--test-dir', 'build/asan', '-C', 'RelWithDebInfo', '-R', pattern, '--output-on-failure'],
        extra={'PATH': str(runtime) + os.pathsep + ENV['PATH']})


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--regression', action='store_true')
    parser.add_argument('--reference', action='store_true')
    parser.add_argument('--asan', action='store_true')
    parser.add_argument('--differential', action='store_true')
    parser.add_argument('--resume', action='store_true', help='Retain successful command results in this build tree.')
    args = parser.parse_args()
    if args.resume and (OUT / 'commands.json').exists():
        records = json.loads((OUT / 'commands.json').read_text())
    if args.regression:
        regression()
    if args.reference:
        run('reference', [sys.executable, '-m', 'pytest', 'tests', '-q'], ROOT / 'Native',
            {'PYTHONPATH': str(ROOT / 'Native')})
    if args.asan:
        asan()
    if args.differential:
        differential()
