"""Run with the clean wheel environment, with no ABCURVES_LIBRARY/PYTHONPATH."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

from abcurves_native import Library, Model, Planner, Renderer, Report, SampleBuffer, Vec2

ROOT = Path(__file__).resolve().parents[2]
library = Library()
binary = Path(library.path).resolve()
assert binary.is_relative_to(Path(sys.prefix).resolve()), binary
assert importlib.util.find_spec('numpy') is None
assert importlib.util.find_spec('abcurves') is None
results = {}
for variant in (32, 16, 8):
    with Model(ROOT / f'lib/models/{variant}ms', library=library) as model:
        with Planner(model) as planner:
            planner.update_target((320, 120), 0)
            output = SampleBuffer(128)
            assert planner.advance_into(128000, output) == 128
            assert output[-1].timestamp_us == 128000
            results[str(variant)] = dict(samples=len(output), sha256=model.info.sha256.decode())
fixture = ROOT / 'tests/baselines/renderer/32ms_120fps_p0_s7_mixed_irregular_same_input.fixture'
rows = fixture.read_text().splitlines()
profile = [tuple(map(int, row.split())) for row in rows[1:257]]
with Renderer(ROOT / 'Native/models/renderer_global_h80.bin', library=library) as renderer:
    renderer.set_profile(profile)
    renderer.reset(7)
    reports = renderer.step_batch([(.1, .03)] * 128)
    assert len(reports) == 128
result = dict(python=sys.version, library=str(binary), dll_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
              numpy_installed=False, original_reference_installed=False, variants=results, renderer_reports=128)
(ROOT / 'build/package_smoke.json').write_text(json.dumps(result, indent=2))
print(json.dumps(result, indent=2))
