"""Execute README/calibration examples and check headline measurement claims.

Uses the public JSON receipts produced by report.py and the built Windows DLL.
Does not change production code, fixture expectations, or README contents.
"""
from __future__ import annotations

import contextlib
import io
import json
import math
import os
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]


def read(name):
    return json.loads((ROOT / "docs/evidence" / name).read_text(encoding="utf-8"))


def number(cell):
    return float(re.match(r"[\d.]+", cell).group())


def main():
    os.chdir(ROOT)
    readme = (ROOT / "README.md").read_text(encoding="utf-8")
    performance = read("reference-performance.json")["variants"]
    accuracy_receipt = read("reference-accuracy.json")
    accuracy = accuracy_receipt["legacy"]
    labels = {
        "Initialization": ("initialize_loaded", 32, 1),
        "Fresh decision + first sample": ("fresh_decision", 32, 1),
        "Motor inference alone": ("motor_inference", 32, 1),
    }
    timing_rows = accuracy_rows = 0
    for line in readme.splitlines():
        if not line.startswith("| "):
            continue
        cells = [cell.strip().replace("**", "") for cell in line.strip("|").split("|")]
        if cells[0] in labels:
            operation, variant, divisor = labels[cells[0]]
        elif match := re.fullmatch(r"Complete commitment \((32|16|8) ms\)", cells[0]):
            operation, variant, divisor = "complete_commitment", int(match[1]), 1
        else:
            if re.match(r"(32|16|8) ms$", cells[0]) and cells[1] == "272,000":
                value = accuracy[cells[0].split()[0]]
                displacement = value["displacement"]
                assert int(cells[1].replace(",", "")) == displacement["samples"]
                assert len(cells) == 4
                assert math.isclose(number(cells[2]), displacement["maximum_absolute"], rel_tol=.001)
                assert math.isclose(number(cells[3]), value["max_cumulative_displacement_deviation"], rel_tol=.001)
                accuracy_rows += 1
            continue
        measured = performance[str(variant)][operation]
        for index, language in ((1, "python"), (2, "cpp")):
            assert abs(number(cells[index]) - measured[language]["median"] / divisor) <= .00501
        assert abs(number(cells[3]) - measured["speedup"]) <= .00501
        timing_rows += 1
    assert timing_rows == 6 and accuracy_rows == 3
    assert "%" not in readme
    assert sum(v["displacement"]["samples"] for v in accuracy.values()) == 816000
    assert all(all(v["diagnostic_checks"].values()) for v in accuracy.values())
    renderer = accuracy_receipt["renderer"]
    assert sum(v["same_input"]["reports"] for v in renderer.values()) == 144000
    assert all(v["same_input"]["mismatches"] == 0 for v in renderer.values())
    assert sum(v["independent_planners"]["mismatches"] for v in renderer.values()) == 9
    assert max(v["independent_planners"]["maximum_cumulative_difference"] for v in renderer.values()) == 1
    assert max(v["independent_planners"]["longest_cumulative_divergence_samples"] for v in renderer.values()) == 3
    assert all(v["independent_planners"]["all_final_cumulative_equal"] for v in renderer.values())

    sys.path.insert(0, str(ROOT / "API/python/src"))
    os.environ.setdefault("ABCURVES_LIBRARY", str(ROOT / "build/api/Release/abcurves.dll"))
    examples = re.findall(r"```python\n(.*?)```", readme, re.S)
    assert len(examples) == 1
    scope = {}
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        exec(examples[0], scope)
    segment = scope["segment"]
    assert len(segment.samples) == len(output.getvalue().splitlines()) == 16
    assert segment.samples[0].timestamp_us == 1000 and segment.samples[-1].timestamp_us == 16000
    assert scope["diagnostics"].total_segments == 1

    calibration = (ROOT / "docs/guides/calibration.md").read_text(encoding="utf-8")
    scope = {}
    exec(re.findall(r"```python\n(.*?)```", calibration, re.S)[0], scope)
    k = scope["k"]
    header = (ROOT / "API/c/include/abcurves.h").read_text()
    assert float(re.search(r"#define ABC_COMMON_RADIANS_PER_COUNT (.+)", header).group(1)) == k
    assert re.search(r"#define ABC_ABI_MINOR 5u", header)
    degrees = k * 180 / math.pi
    assert round(degrees, 5) == .02011
    assert round(100 * degrees * 20, 2) == 40.22
    assert math.isclose(math.atan(math.tan(math.radians(90) / 2)), math.radians(45))
    maximum = max(v["max_cumulative_displacement_deviation"] for v in accuracy.values())
    assert round(maximum, 6) == .001374
    assert math.isclose(maximum * degrees, .0000276, rel_tol=.001)
    assert round(maximum * degrees, 6) == .000028

    # Execute the guide's ROI mapping and illustrative rotation convention.
    roi = dict(roi_x=420, roi_y=300, roi_left=960, roi_top=400)
    exec(re.findall(r"```text\n(full_width = .*?)```", calibration, re.S)[0], roi)
    assert roi["full_x"] == 1380 and roi["full_y"] == 700
    assert roi["dx"] == 100 and roi["dy"] == 20
    camera = dict(dx=roi["dx"], dy=roi["dy"], width=roi["full_width"], height=roi["full_height"],
        HFOV=math.pi / 2, VFOV=2 * math.atan((1440 / 2560) * math.tan(math.pi / 4)),
        atan=math.atan, atan2=math.atan2, tan=math.tan, sqrt=math.sqrt)
    exec(re.findall(r"```text\n(x = .*?)```", calibration, re.S)[0], camera)
    exec(re.findall(r"```text\n(yaw   = .*?)```", calibration, re.S)[0], camera)
    x, y, yaw, pitch = (camera[name] for name in ("x", "y", "yaw", "pitch"))
    norm = math.sqrt(1 + x*x + y*y)
    rotated = (math.sin(yaw)*math.cos(pitch), math.sin(pitch), math.cos(yaw)*math.cos(pitch))
    assert all(math.isclose(actual, expected, rel_tol=1e-14) for actual, expected in zip(rotated, (x/norm, y/norm, 1/norm)))
    for variant in (32, 16, 8):
        manifest = json.loads((ROOT / f"lib/models/{variant}ms/manifest.json").read_text())
        assert manifest["commitment_ms"] == variant and manifest["constants"]["sample_us"] == 1000
    result = dict(performance_table_rows=timing_rows, accuracy_table_rows=accuracy_rows,
        readme_example="Executed verbatim: 16 samples, timestamps and diagnostics checked",
        calibration_example="Executed verbatim; units and numeric conversions checked",
        roi_and_ray_examples="Executed; full-frame offsets/signs and example yaw/pitch ray reconstruction checked",
        maximum_cumulative_deviation_common_counts=maximum,
        maximum_cumulative_deviation_degrees=maximum * degrees,
        radians_per_count=k, degrees_per_count=degrees,
        device_counts_for_100_common_at_20_per_degree=100 * degrees * 20,
        perspective_pixel_example_counts=scope["target_error_counts"], abi="1.4")
    out = ROOT / "build/reference-comparison-final/docs-checks.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
