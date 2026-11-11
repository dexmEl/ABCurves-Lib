"""Rerun upstream Continuous Planner demos against the native library.

Downloads only hash-pinned upstream Python/model files into build/. Never
changes runtime source, model weights, or the frozen original demo payloads.
"""
from __future__ import annotations

import os
for variable in ('OMP_NUM_THREADS', 'OPENBLAS_NUM_THREADS', 'MKL_NUM_THREADS', 'NUMBA_NUM_THREADS'):
    os.environ[variable] = '1'

import argparse
from datetime import datetime, timezone
import importlib.metadata
import json
from pathlib import Path
import platform
import subprocess
import sys
import urllib.request
import zlib

import numpy as np
from common import DATA, ROOT, read_json, sha, source_sha, upstream

REPO = ROOT.parent
CHECKS = ['timestamps', 'decisions', 'motor_counts', 'modes', 'selected_heads',
          'decision_boundaries', 'receipt_times', 'commitment_remainders']


def reference(root):
    pin = read_json(DATA / 'reference.json')
    for name, digest in pin['files'].items():
        path = root / name
        if not path.is_file() or sha(path.read_bytes()) != digest:
            url = f"https://raw.githubusercontent.com/optima-manent/ABCurves/{pin['commit']}/{name}"
            raw = urllib.request.urlopen(url, timeout=60).read()
            if sha(raw) != digest:
                raise ValueError('Upstream source digest mismatch: ' + name)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(raw)
    sys.path.insert(0, str(root))
    from abcurves._continuous.backend import Assets
    from abcurves._continuous.planner import Planner
    return pin, Planner(Assets(root / 'models/continuous'))


def replay_python(policy, seed, start, history, target):
    from abcurves._continuous.runtime import MovementRuntime
    from abcurves._continuous import kernels
    runtime = MovementRuntime(policy, seed=seed, initial_xy=start, history=history)
    # Explicit matched protocol: sample k is received at k ms; no future
    # target interpolation. Both runtimes queue the identical timestamped events.
    for k, point in enumerate(target[:-1]):
        runtime.update_target(point, k * 1000)
    n = len(target) - 1
    positions = np.empty((n + 1, 2), dtype='<f8'); positions[0] = start
    diagnostics = np.empty((n, len(CHECKS)), dtype='<i8')
    head = -1; decision_us = -1
    for k in range(n):
        prior = policy.motor_evaluations; decisions = runtime.decisions
        incoming = runtime._history[(runtime._cursor + 639) % 640].copy()
        result = runtime.advance((k + 1) * 1000)
        positions[k + 1] = result['xy'][0]
        if runtime.decisions > decisions:
            head = -1; decision_us = k * 1000
        if policy.motor_evaluations > prior:
            heads = kernels.decode_geometry(policy.engine._motor.coefficients, incoming,
                                            policy.mean_basis, policy.mean_carry)
            matches = np.flatnonzero(np.all(heads == policy.previous, axis=(1, 2)))
            if len(matches) != 1:
                raise ValueError('Cannot uniquely identify original movement head')
            head = int(matches[0])
        diagnostics[k] = [result['time_us'][0], runtime.decisions, policy.motor_evaluations,
                          policy.mode if policy.mode is not None else 2, head, decision_us,
                          runtime.receipt_us if runtime._target is not None else -1,
                          32 - runtime._pending_index]
    return positions, diagnostics


def replay_native(model, seed, start, history, target):
    from abcurves_native import Planner, SampleBuffer
    n = len(target) - 1
    positions = np.empty((n + 1, 2), dtype='<f8'); positions[0] = start
    diagnostics = np.empty((n, len(CHECKS)), dtype='<i8')
    with Planner(model, seed=seed, initial_position=start, history=history) as planner:
        for k, point in enumerate(target[:-1]):
            planner.update_target(point, k * 1000)
        buffer = SampleBuffer(1)
        for k in range(n):
            if planner.advance_into((k + 1) * 1000, buffer) != 1:
                raise ValueError('Native replay did not emit exactly one sample')
            sample = buffer.array[0]; state = planner.state
            positions[k + 1] = tuple(sample.position)
            diagnostics[k] = [sample.timestamp_us, state.decision_count, state.motor_evaluations,
                              state.mode, state.selected_head, state.last_decision_us,
                              state.last_target_receipt_us, state.pending_samples]
    return positions, diagnostics


def metrics(a, b):
    error = b - a
    exact = np.all(a == b, axis=1)
    return dict(samples=len(a), exact_pairs=int(exact.sum()),
                max_abs=float(np.abs(error).max()), mae=float(np.abs(error).mean()),
                rmse=float(np.sqrt(np.square(error).mean())),
                squared_error_sum=float(np.square(error).sum()),
                first_difference_ms=int(np.flatnonzero(~exact)[0] + 1) if not exact.all() else None)


def aggregate(entries):
    result = dict(runs=len(entries), samples=sum(e['position']['samples'] for e in entries))
    for kind in ('position', 'displacement'):
        result[kind] = dict(max_abs=max(e[kind]['max_abs'] for e in entries),
                            exact_pairs=sum(e[kind]['exact_pairs'] for e in entries),
                            mae=sum(e[kind]['mae'] * e[kind]['samples'] for e in entries) / result['samples'],
                            rmse=float(np.sqrt(sum(e[kind]['squared_error_sum'] for e in entries) / (2 * result['samples']))))
    result['checks'] = {key: all(e['checks'][key] for e in entries) for key in CHECKS}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, required=True, help='Fresh Release C ABI library')
    parser.add_argument('--reference', type=Path, default=REPO / 'build/upstream-showcase')
    args = parser.parse_args()
    library = args.library.resolve()
    inputs = upstream()
    pin, policy = reference(args.reference.resolve())
    policy.prewarm()
    sys.path.insert(0, str(REPO / 'API/python/src'))
    from abcurves_native import Model
    examples = []; all_runs = []
    with Model(REPO / 'lib/models/32ms', library=library) as model:
        for identifier, (meta, provenance, payload, arrays) in inputs.items():
            duration = meta['duration']
            target = np.asarray(arrays['target'], dtype='<f8').reshape(-1, 2)
            prefix = np.asarray(arrays['prefix'], dtype='<f8').reshape(-1, 2)
            # The website publishes prefix POSITIONS. Difference those exact
            # binary64 values once, then give the same raw bins to both engines.
            history = np.diff(prefix, axis=0) if len(prefix) > 1 else np.zeros((160, 2))
            if history.shape != (160, 2) or target.shape != (duration + 1, 2):
                raise ValueError('Unexpected demo prefix/target shape: ' + identifier)
            start = np.asarray(meta['start'], dtype='<f8')
            if not np.array_equal(prefix[-1], start):
                raise ValueError('Prefix does not end at the published anchor: ' + identifier)
            layout = {}; raw = bytearray()
            def pack(name, value):
                value = np.ascontiguousarray(value, dtype='<f8')
                raw.extend(b'\0' * (-len(raw) % 8))
                layout[name] = dict(offset=len(raw), shape=list(value.shape), type='<f8')
                raw.extend(value.tobytes())
            pack('target', target); pack('radius', np.array(arrays['radius']).reshape(-1, 2)); pack('prefix', prefix)
            runs = []
            for index, seed in enumerate(provenance['seeds']):
                original, original_diag = replay_python(policy, seed, start, history, target)
                native, native_diag = replay_native(model, seed, start, history, target)
                checks = {name: bool(np.array_equal(original_diag[:, i], native_diag[:, i])) for i, name in enumerate(CHECKS)}
                entry = dict(seed=seed, position=metrics(original[1:], native[1:]),
                             displacement=metrics(np.diff(original, axis=0), np.diff(native, axis=0)), checks=checks,
                             first_check_difference_ms={name: int(np.flatnonzero(original_diag[:, i] != native_diag[:, i])[0] + 1)
                                if not checks[name] else None for i, name in enumerate(CHECKS)})
                pack('original' + str(index), original); pack('native' + str(index), native)
                pack('originalDiagnostics' + str(index), original_diag)
                pack('nativeDiagnostics' + str(index), native_diag)
                runs.append(entry); all_runs.append(entry)
                print(identifier, seed, 'max position', entry['position']['max_abs'], 'checks', all(checks.values()), flush=True)
            blob = zlib.compress(bytes(raw), level=9)
            filename = 'runs/' + meta['sourceId'] + '.bin'
            path = DATA / filename; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(blob)
            combined = np.concatenate([target] + [np.frombuffer(raw, dtype='<f8', count=(duration + 1) * 2,
                offset=layout[f'{kind}{i}']['offset']).reshape(-1, 2) for i in range(4) for kind in ('original', 'native')])
            bounds = [float(combined[:, 0].min()), float(combined[:, 1].min()), float(combined[:, 0].max()), float(combined[:, 1].max())]
            examples.append(dict(id=identifier, sourceId=meta['sourceId'], title=meta['title'], human=meta['human'],
                duration=duration, start=meta['start'], bounds=bounds, thumbnail=meta['thumbnail'],
                file=filename, bytes=len(raw), sha256=sha(raw), compressed_sha256=sha(blob), layout=layout, runs=runs,
                input_payload_sha256=provenance['packed_motion_sha256'],
                prefix_kind='published raw prefix' if len(prefix) > 1 else 'rest (zero history)',
                history_sha256=sha(history.astype('<f8').tobytes()), target_sha256=sha(target.tobytes())))
    sources = {}; source_bytes = {}
    for folder in ('lib/src', 'lib/include', 'API/c/src', 'API/c/include', 'API/python/src'):
        for path in sorted((REPO / folder).rglob('*')):
            if path.is_file() and '__pycache__' not in path.parts:
                name = path.relative_to(REPO).as_posix()
                sources[name] = source_sha(path)
                source_bytes[name] = sha(path.read_bytes())
    output = dict(schema='abcurves.continuous.comparison.v1',
        recorded_utc=datetime.now(timezone.utc).isoformat(),
        upstream=dict(repository=pin['repository'], commit=pin['commit'], files=pin['files']),
        native=dict(commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=REPO, text=True).strip(),
                    library_sha256=sha(library.read_bytes()), sources=sources,
                    source_hash_policy='sources: SHA-256 with CRLF normalized to LF; source_bytes_sha256: exact measurement checkout bytes',
                    source_bytes_sha256=source_bytes,
                    model_sha256=sha((REPO / 'lib/models/32ms/model.abcmodel').read_bytes())),
        environment=dict(platform=platform.platform(), python=platform.python_version(),
                         packages={name: importlib.metadata.version(name) for name in ('numpy', 'numba', 'scipy')},
                         thread_pools=1, build='Release; ABC_PROFILE=OFF; strict floating point'),
        protocol=dict(commitment_ms=32, sample_us=1000, units='common angular counts',
                      target='Published target[k] received at k * 1000 us for k = 0 .. duration-1; no interpolation.',
                      prefix='Recorded examples: 160 raw displacements from the published 161 prefix positions. Designed targets: rest, zero history. No W5 filtering.',
                      output='Fresh smooth planner reruns; no Renderer. Original website integer reports are not the baseline.',
                      scope='18 curated demos, four published planner seeds each. Target receipt schedule and raw report/profile inputs for the original frozen renders are not fully published. This explicit matched protocol is not a reproduction of those renders.'),
        summary=aggregate(all_runs), examples=examples)
    (DATA / 'comparison.json').write_text(json.dumps(output, ensure_ascii=False, indent=2, allow_nan=False) + '\n', encoding='utf8')
    print(json.dumps(output['summary'], indent=2))


if __name__ == '__main__':
    main()
