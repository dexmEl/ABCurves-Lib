"""Validate measured receipts and generate comparison reports under build/.

Run after compare.py, extended.py and differential/qualify.py. Does not run a
benchmark or change any expected fixture, model, or production source.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def model_identities():
    models = {}
    for c, name in ((32, "continuous"), (16, "continuous-16"), (8, "continuous-8")):
        native = ROOT / f"lib/models/{c}ms"
        original = ROOT / "Native/models" / name
        manifest = read(native / "manifest.json")
        assert sha(native / "model.abcmodel") == manifest["model_sha256"]
        assert sha(original / "manifest.json") == manifest["source_manifest_sha256"]
        with np.load(original / "weights.npz", allow_pickle=False) as weights:
            assert set(weights.files) == set(manifest["tensors"])
            for name, entry in manifest["tensors"].items():
                array = np.ascontiguousarray(weights[name])
                assert list(array.shape) == entry["shape"]
                assert str(array.dtype) == entry["dtype"]
                assert hashlib.sha256(array.tobytes()).hexdigest() == entry["sha256"]
        models[str(c)] = dict(native_sha256=manifest["model_sha256"],
            original_weights_sha256=sha(original / "weights.npz"),
            source_manifest_sha256=manifest["source_manifest_sha256"],
            identical_tensor_count=manifest["tensor_count"])
    return models


def validate_accuracy_arrays(results, cases, raw_cases):
    for name, entry in {**cases, **raw_cases}.items():
        stem = results / "accuracy" / name
        original = np.load(str(stem) + "_python.npz")["rows"]
        native = np.loadtxt(str(stem) + ".csv", delimiter=",", skiprows=1)
        assert np.isfinite(original).all() and np.isfinite(native).all()
        for kind, columns in (("displacement", slice(3, 5)), ("position", slice(1, 3))):
            ref, actual = original[:, columns].copy(), native[:, columns].copy()
            error = actual - ref
            metrics = entry[kind]
            assert metrics["samples"] == len(ref)
            assert metrics["exact_samples"] == int(np.all(ref == actual, axis=1).sum())
            assert metrics["bitwise_samples"] == int(np.all(ref.view(np.uint64) == actual.view(np.uint64), axis=1).sum())
            for key, value in (("maximum_absolute", np.abs(error).max()),
                               ("mean_absolute", np.abs(error).mean()),
                               ("rmse", np.sqrt(np.mean(error ** 2)))):
                assert math.isclose(metrics[key], float(value), rel_tol=1e-12, abs_tol=1e-18), (name, kind, key)
        assert np.array_equal(original[:, 0], native[:, 0])
        assert np.array_equal(original[:, 5:], native[:, 6:])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", type=Path, default=ROOT / "build/reference-comparison-final")
    parser.add_argument("--out", type=Path, help="Generated report directory; defaults to RESULTS/reports (not public documentation).")
    args = parser.parse_args()
    results = args.results.resolve()
    destination = (args.out or results / "reports").resolve()
    destination.mkdir(parents=True, exist_ok=True)
    environment = read(results / "performance/environment.json")
    accuracy_environment = read(results / "accuracy/environment.json")
    for key in ("runtime_source_sha256", "native_executable_sha256", "replay_sha256", "python", "numpy", "packages", "threads"):
        assert environment[key] == accuracy_environment[key], f"Mismatched campaigns: {key}"
    for name, value in environment["runtime_source_sha256"].items():
        assert sha(ROOT / name) == value, f"Runtime changed since measurement: {name}"
    provenance = dict(measured_date=environment["recorded_utc"][:10], original="Local abcurves 2.0.0 Python reference under Native/, Numba + BLAS",
        upstream_base="e197441f8d343d3b4a4069c7cae20764d0953d15",
        optimized="C++20 engine 0.2.0; API ABI 1.4; direct static linkage for performance",
        variant_note="16/8 ms weights are locally trained bundles, compared with the same weights in both runtimes.",
        models=model_identities(), environment=environment,
        tool_sha256={p.relative_to(ROOT).as_posix(): sha(p) for p in sorted(Path(__file__).parent.glob('*')) if p.suffix in ('.py','.cpp','.txt')},
        raw_results="build/reference-comparison-final/ (generated locally; not shipped)")
    write(destination / "reference-environment.json", provenance)

    performance = read(results / "performance/summary.json")
    performance["provenance"] = "reference-environment.json"
    performance["measurement_note"] = "Target, selected decode and history preparation: 16-call batch averages; buffered sample: 4-call averages. Other rows: individual elapsed times. Warm filesystem and JIT."
    performance["raw_csv_sha256"] = {p.name: sha(p) for p in sorted((results / "performance").glob("*.csv"))}
    lines = []
    labels = dict(model_load="Load model bundle", initialize_loaded="Initialize, loaded model", reset_session="Reset session",
        target_update="Queue target (legacy API)", fresh_decision="Fresh decision + first sample", complete_commitment="Complete commitment",
        buffered_sample="Buffered 1 ms sample", motor_features="Motor history features", event_features="Event history features",
        motor_inference="Motor neural inference", selector_geometry_network="Selector geometry + network", hazard_inference="Hazard inference",
        brake_inference="Brake inference", decode_selected="Decode selected head", brake_path="Brake path endpoints",
        prepare_history="Prepare raw 164-sample prefix", renderer_load="Load Renderer image", renderer_profile="Prepare 256-report profile",
        renderer_begin="Begin Renderer stream", renderer_step="Renderer report")
    for c, operations in performance["variants"].items():
        assert set(operations) == set(labels)
        import csv
        measured = {"python": {}, "cpp": {}}
        for language in measured:
            for run in range(performance["runs"]):
                path = results / "performance" / f"{c}_{language}_r{run}.csv"
                with path.open(newline="") as stream:
                    for row in csv.DictReader(stream):
                        measured[language].setdefault(row["operation"], []).append(float(row["us"]))
        lines += [f"### {c} ms model", "", "All times are **microseconds**. Each paired cell is **Python / C++**. N is the measurement count **per implementation**; batched rows contain batch averages.", "",
            "| Operation | N | Median P / C++ | Mean P / C++ | p95 P / C++ | p99 P / C++ | Maximum P / C++ | Median speedup |",
            "|---|---:|---:|---:|---:|---:|---:|---:|"]
        for name, entry in operations.items():
            original, native = entry["python"], entry["cpp"]
            for language, stat in (("python", original), ("cpp", native)):
                values = np.array(measured[language][name])
                assert len(values) == stat["count"]
                for key, value in (("mean", values.mean()), ("median", np.median(values)),
                                   ("p95", np.percentile(values, 95)), ("p99", np.percentile(values, 99)), ("maximum", values.max())):
                    assert math.isclose(stat[key], float(value), rel_tol=1e-12, abs_tol=1e-12)
            assert original["count"] == native["count"]
            assert math.isclose(entry["speedup"], original["median"] / native["median"])
            assert all(math.isfinite(s[k]) and s[k] > 0 for s in (original, native) for k in ("median", "mean", "p95", "p99", "maximum"))
            times = [f"{original[k]:.3f} / {native[k]:.3f}" for k in ("median", "mean", "p95", "p99", "maximum")]
            lines.append(f"| {labels[name]} | {original['count']:,} | " + " | ".join(times) + f" | {entry['speedup']:.2f}× |")
        lines.append("")
    write(destination / "reference-performance.json", performance)
    (destination / "performance.md").write_text('# Complete measured performance tables\n\n' + "\n".join(lines).strip() + '\n', encoding='utf-8')

    legacy = read(results / "accuracy/summary.json")
    cases = read(results / "accuracy/cases.json")
    raw = read(results / "accuracy/raw-prefix.json")
    validate_accuracy_arrays(results, cases, raw["cases"])
    renderer = read(results / "renderer/summary.json")
    components = read(results / "components/components.json")
    assert len(cases) == 102 and len(raw["cases"]) == 9
    for name, entry in cases.items():
        assert sha(ROOT / f"tests/baselines/legacy/{name}.fixture") == entry["fixture_sha256"]
    for value in legacy.values():
        assert all(value["diagnostic_checks"].values())
        for kind in ("displacement", "position"):
            value[kind]["exact_percent"] = value[kind]["exact_samples"] / value[kind]["samples"] * 100
    for entry in raw["cases"].values():
        assert entry["timestamps_equal"] and entry["diagnostics_equal"]
    assert all(all(entry["exact"]) for entry in components["rng"].values())
    assert all(stage["pass"] for entry in components["components"].values() for stage in entry["stages"].values())
    rendered = {}
    for c in (32, 16, 8):
        rendered[str(c)] = {}
        for kind in ("same_input", "independent_planners"):
            entries = [value[kind] for name, value in renderer.items() if name.startswith(f"{c}ms")]
            rendered[str(c)][kind] = dict(reports=sum(v["reports"] for v in entries),
                exact_reports=sum(v["exact_reports"] for v in entries),
                mismatches=sum(v["mismatches"] for v in entries),
                maximum_cumulative_difference=max(v["maximum_cumulative_difference"] for v in entries),
                longest_cumulative_divergence_samples=max(v["longest_cumulative_divergence_samples"] for v in entries),
                all_final_cumulative_equal=all(v["final_cumulative_difference"] == [0, 0] for v in entries))
    accuracy = dict(schema=1, provenance="reference-environment.json", legacy=legacy,
        raw_prefix=raw, renderer=rendered, renderer_cases=renderer,
        component_cases=len(components["components"]), component_cases_within_tolerances=True,
        rng_seed_cases=components["rng"],
        criteria="Exact displacement/position sample = both X/Y float64 components numerically equal. Bitwise counts inspect uint64 representations. Errors are per-axis common counts; MAE/RMSE average over both axes.")
    write(destination / "reference-accuracy.json", accuracy)
    write(destination / "reference-accuracy-cases.json", cases)
    write(destination / "reference-components.json", components)
    lines = ["| Model | Samples | Exact ΔX/ΔY pairs | Exact % | Max displacement error | Mean absolute error | RMSE |",
        "|---|---:|---:|---:|---:|---:|---:|"]
    for c, value in legacy.items():
        d = value["displacement"]
        lines.append(f"| {c} ms | {d['samples']:,} | {d['exact_samples']:,} | {d['exact_percent']:.2f}% | {d['maximum_absolute']:.9g} | {d['mean_absolute']:.9g} | {d['rmse']:.9g} |")
    lines += ["", "| Model | Exact position pairs | Max position error | Position MAE | Position RMSE | Max cumulative Δ deviation |", "|---|---:|---:|---:|---:|---:|"]
    for c, value in legacy.items():
        p = value["position"]
        lines.append(f"| {c} ms | {p['exact_samples']:,} | {p['maximum_absolute']:.9g} | {p['mean_absolute']:.9g} | {p['rmse']:.9g} | {value['max_cumulative_displacement_deviation']:.9g} |")
    (destination / "numerical-accuracy.md").write_text('# Detailed measured accuracy\n\n' + "\n".join(lines) + '\n', encoding='utf-8')
    print("Verified 60 operation/model comparisons, 102 legacy + 9 raw-prefix replays, 18 Renderer streams, 36 component cases, 5 RNG seeds, 129 identical model tensors.")


if __name__ == "__main__":
    main()
