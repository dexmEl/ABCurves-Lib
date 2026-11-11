"""Build the static comparison site from verified frozen runs (standard library only)."""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import shutil
import zlib

from common import DATA, ROOT, read_json, sha, source_sha, unpack, upstream

CHECKS = ['timestamps', 'decisions', 'motor_counts', 'modes', 'selected_heads',
          'decision_boundaries', 'receipt_times', 'commitment_remainders']


def compare_metric(a, b, duration, displacement=False):
    errors = []; exact = 0; first = None
    for tick in range(1, duration + 1):
        pair = []
        for axis in (0, 1):
            i = tick * 2 + axis
            x = a[i] - a[i - 2] if displacement else a[i]
            y = b[i] - b[i - 2] if displacement else b[i]
            pair.append(y - x)
        errors.extend(pair)
        if pair == [0., 0.]:
            exact += 1
        elif first is None:
            first = tick
    square = math.fsum(e * e for e in errors)
    return dict(samples=duration, exact_pairs=exact, max_abs=max(map(abs, errors)),
                mae=math.fsum(map(abs, errors)) / len(errors), rmse=math.sqrt(square / len(errors)),
                squared_error_sum=square, first_difference_ms=first)


def assert_metrics(actual, recorded):
    if actual.keys() != recorded.keys():
        raise ValueError('Changed metric schema')
    for key, value in actual.items():
        equal = math.isclose(value, recorded[key], rel_tol=1e-12, abs_tol=1e-30) if isinstance(value, float) else value == recorded[key]
        if not equal:
            raise ValueError(f'Metric differs from raw samples: {key}: {value} != {recorded[key]}')


def validate(report):
    original = upstream()
    if (report['schema'] != 'abcurves.continuous.comparison.v1'
            or [m['id'] for m in report['examples']] != list(original)
            or report['upstream'] != read_json(DATA / 'reference.json')):
        raise ValueError('Wrong comparison corpus or source version')
    if report['protocol']['commitment_ms'] != 32 or report['protocol']['sample_us'] != 1000:
        raise ValueError('Changed commitment or sample cadence')
    for name, digest in report['native']['sources'].items():
        if source_sha(ROOT.parent / name) != digest:
            raise ValueError('Native source changed; regenerate comparison: ' + name)
    if sha((ROOT.parent / 'lib/models/32ms/model.abcmodel').read_bytes()) != report['native']['model_sha256']:
        raise ValueError('Native model changed; regenerate comparison')
    runs = []
    for example in report['examples']:
        identifier = example['id']; meta, provenance, _, inputs = original[identifier]
        path = (DATA / example['file']).resolve()
        if path.parent != (DATA / 'runs').resolve() or path.suffix != '.bin':
            raise ValueError('Invalid motion path')
        blob = path.read_bytes(); raw = zlib.decompress(blob)
        if sha(blob) != example['compressed_sha256'] or sha(raw) != example['sha256'] or len(raw) != example['bytes']:
            raise ValueError('Corrupt comparison: ' + identifier)
        arrays = unpack(raw, example['layout']); duration = example['duration']
        if duration != meta['duration'] or example['start'] != meta['start']:
            raise ValueError('Changed duration or anchor: ' + identifier)
        for key in ('target', 'radius', 'prefix'):
            if arrays[key] != inputs[key]:
                raise ValueError('Changed original input: ' + identifier + '/' + key)
        if len(example['runs']) != 4 or [r['seed'] for r in example['runs']] != provenance['seeds']:
            raise ValueError('Changed original seed selection')
        for index, run in enumerate(example['runs']):
            a, b = arrays['original' + str(index)], arrays['native' + str(index)]
            if len(a) != (duration + 1) * 2 or len(b) != len(a) or a[:2] != meta['start'] or b[:2] != meta['start']:
                raise ValueError('Invalid output length or initial anchor')
            assert_metrics(compare_metric(a, b, duration), run['position'])
            assert_metrics(compare_metric(a, b, duration, displacement=True), run['displacement'])
            da, db = arrays['originalDiagnostics' + str(index)], arrays['nativeDiagnostics' + str(index)]
            if len(da) != duration * 8 or len(db) != len(da):
                raise ValueError('Incomplete diagnostics')
            for i, key in enumerate(CHECKS):
                first = next((tick + 1 for tick in range(duration) if da[tick * 8 + i] != db[tick * 8 + i]), None)
                if run['checks'][key] != (first is None) or run['first_check_difference_ms'][key] != first:
                    raise ValueError('Diagnostic summary differs from raw traces')
            if da[::8] != list(range(1000, (duration + 1) * 1000, 1000)) or db[::8] != da[::8]:
                raise ValueError('Wrong output timestamps')
            runs.append(run)
        print('Verified', identifier, flush=True)
    summary = report['summary']; n = sum(r['position']['samples'] for r in runs)
    if summary['samples'] != n or summary['runs'] != len(runs):
        raise ValueError('Incorrect aggregate sample/run counts')
    for kind in ('position', 'displacement'):
        expected = dict(max_abs=max(r[kind]['max_abs'] for r in runs),
                        exact_pairs=sum(r[kind]['exact_pairs'] for r in runs),
                        mae=math.fsum(r[kind]['mae'] * r[kind]['samples'] for r in runs) / n,
                        rmse=math.sqrt(math.fsum(r[kind]['squared_error_sum'] for r in runs) / (2 * n)))
        assert_metrics(expected, summary[kind])
    if summary['checks'] != {key: all(r['checks'][key] for r in runs) for key in CHECKS}:
        raise ValueError('Incorrect aggregate diagnostics')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT.parent / 'build/web')
    args = parser.parse_args()
    report = read_json(DATA / 'comparison.json')
    validate(report)
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    (output / 'data/runs').mkdir(parents=True, exist_ok=True)
    for example in report['examples']:
        shutil.copyfile(DATA / example['file'], output / 'data' / example['file'])
    shutil.copyfile(DATA / 'comparison.json', output / 'data/comparison.json')
    for name in ('LICENSE', 'NOTICE'):
        shutil.copyfile(ROOT / name, output / name)
    template = (ROOT / 'src/index.html').read_text(encoding='utf8')
    substitutions = {'__STYLES__': (ROOT / 'src/styles.css').read_text(encoding='utf8'),
                     '__APP__': (ROOT / 'src/app.js').read_text(encoding='utf8'),
                     '__COMPARISON__': json.dumps(report, ensure_ascii=False, separators=(',', ':')).replace('<', '\\u003c')}
    for key, value in substitutions.items():
        if template.count(key) != 1:
            raise ValueError('Expected exactly one placeholder: ' + key)
        template = template.replace(key, value)
    (output / 'index.html').write_text(template, encoding='utf8', newline='\n')
    (output / '.nojekyll').touch()
    print(f'Built {output / "index.html"}: {len(report["examples"])} examples, {report["summary"]["runs"]} runs, {report["summary"]["samples"]:,} paired samples')


if __name__ == '__main__':
    main()
