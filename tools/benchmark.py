"""Measure full-segment C ABI latency; optionally pair an earlier DLL/executable.

No deadline or performance-equivalence claim is inferred from these timings.
"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def percentile(values, fraction):
    ordered = sorted(values)
    index = (len(ordered) - 1) * fraction
    lower = int(index)
    return ordered[lower] + (ordered[min(lower + 1, len(ordered)-1)] - ordered[lower]) * (index-lower)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--before', type=Path, help='Directory with earlier abcurves.dll and benchmark executable.')
    parser.add_argument('--runs', type=int, default=3)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    if args.runs < 1:
        parser.error('--runs must be positive')
    executable = (args.before if args.before else ROOT / 'build/api/Release') / 'abc_segment_cabi_benchmark.exe'
    variants = {'after': ROOT / 'build/api/Release'}
    if args.before:
        variants['before'] = args.before
    # With --before, the exact same compiled benchmark runs both DLLs.
    for label, source in variants.items():
        folder = args.out / label
        folder.mkdir(exist_ok=True)
        shutil.copy2(executable, folder / executable.name)
        shutil.copy2(source / 'abcurves.dll', folder / 'abcurves.dll')
    values = {}
    process_measurements = {}
    for repetition in range(args.runs):
        for model in (32, 16, 8):
            order = list(variants) if repetition % 2 == 0 else list(reversed(variants))
            for label in order:
                folder = args.out / label
                stem = folder / f'{model}ms-r{repetition}'
                subprocess.run([str((folder / executable.name).resolve()), str(ROOT / f'lib/models/{model}ms'), str(stem.resolve())], check=True)
                process_measurements.setdefault(str(model), {}).setdefault(label, []).append(json.loads(stem.with_suffix('.json').read_text()))
                with stem.with_suffix('.csv').open(newline='') as stream:
                    for row in csv.DictReader(stream):
                        values.setdefault(str(model), {}).setdefault(label, {}).setdefault(row['stage'], []).append(float(row['us']))
    result = dict(platform=platform.platform(), processor=os.environ.get('PROCESSOR_IDENTIFIER', platform.processor()),
                  runs=args.runs, alternating_order=True, warmup=128, calls_per_stage_per_run=2400,
                  benchmark_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                  allocation_measurement='DLL allocator not intercepted', process_measurements=process_measurements,
                  dll_sha256={label: hashlib.sha256((args.out/label/'abcurves.dll').read_bytes()).hexdigest() for label in variants}, variants={})
    for model, labels in values.items():
        result['variants'][model] = {}
        for label, stages in labels.items():
            result['variants'][model][label] = {
                stage: dict(count=len(v), mean=statistics.mean(v), median=statistics.median(v), p95=percentile(v, .95),
                            p99=percentile(v, .99), maximum=max(v)) for stage, v in stages.items()}
    (args.out / 'summary.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result['variants'], indent=2))


if __name__ == '__main__':
    main()
