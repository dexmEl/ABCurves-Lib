"""Portable, lossless motion packing shared by generation and site builds."""
from __future__ import annotations

import base64
import gzip
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parent
DATA = ROOT / 'data'
TYPES = {'<i2': ('h', 2), '<f4': ('f', 4), '<f8': ('d', 8)}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def source_sha(path):
    """Hash maintained text as Git stores it, independent of checkout CRLF."""
    return sha(path.read_bytes().replace(b'\r\n', b'\n'))


def native_source_paths(repo):
    """Use the tracked source inventory, excluding local packaging/cache files."""
    folders = ('lib/src', 'lib/include', 'API/c/src', 'API/c/include', 'API/python/src')
    names = subprocess.check_output(['git', 'ls-files', '-z', '--', *folders], cwd=repo)
    return [repo / name for name in sorted(names.decode('utf8').split('\0')) if name]


def read_json(path):
    return json.loads(path.read_text(encoding='utf8'))


def unpack(raw, layout):
    arrays = {}
    intervals = []
    for name, spec in layout.items():
        code, width = TYPES[spec['type']]
        if not spec['shape'] or any(type(n) is not int or n <= 0 for n in spec['shape']):
            raise ValueError(f'{name}: invalid shape')
        n = math.prod(spec['shape'])
        start = spec['offset']
        end = start + n * width
        if type(start) is not int or start < 0 or start % 8 or end > len(raw):
            raise ValueError(f'{name}: invalid array interval')
        values = [v for (v,) in struct.iter_unpack('<' + code, raw[start:end])]
        if not all(math.isfinite(v) for v in values):
            raise ValueError(f'{name}: nonfinite motion')
        arrays[name] = values
        intervals.append((start, end))
    intervals.sort()
    if any(a[1] > b[0] for a, b in zip(intervals, intervals[1:])):
        raise ValueError('Overlapping motion arrays')
    return arrays


def upstream():
    catalog = read_json(DATA / 'upstream-catalog.json')
    provenance = read_json(DATA / 'upstream-provenance.json')
    motion = json.loads(gzip.decompress((DATA / 'upstream-motion.json.gz').read_bytes()))
    ids = [m['id'] for m in catalog]
    if (len(ids) != 18 or len(set(ids)) != 18
            or ids != [e['id'] for e in provenance['examples']]
            or ids != list(motion['motions'])
            or any(m['section'] != 'continuous' for m in catalog)):
        raise ValueError('Expected exactly the 18 original Continuous Planner examples in order')
    result = {}
    for meta, entry in zip(catalog, provenance['examples']):
        payload = motion['motions'][meta['id']]
        if sha(json.dumps(payload, separators=(',', ':')).encode()) != entry['payload_json_sha256']:
            raise ValueError('Changed original payload: ' + meta['id'])
        keys = ['id', 'section', 'start', 'factor', 'humanFactor', 'duration', 'prefixDuration', 'human']
        if sha(json.dumps({k: meta[k] for k in keys}, separators=(',', ':')).encode()) != entry['motion_metadata_sha256']:
            raise ValueError('Changed original scale/timing: ' + meta['id'])
        blob = base64.b64decode(payload['blob'], validate=True)
        raw = zlib.decompress(blob)
        if (sha(blob) != entry['compressed_motion_sha256']
                or sha(raw) != payload['sha256'] or sha(raw) != entry['packed_motion_sha256']
                or len(raw) != payload['bytes'] or len(raw) != entry['packed_motion_bytes']):
            raise ValueError('Corrupt original motion: ' + meta['id'])
        result[meta['id']] = (meta, entry, payload, unpack(raw, payload['layout']))
    return result
