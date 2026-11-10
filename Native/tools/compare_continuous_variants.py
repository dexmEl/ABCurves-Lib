"""Apples-to-apples Continuous Planner cadence and trajectory comparison.

The script runs the same causal target schedule and seeds through the frozen
32 ms bundle and the cadence-specific custom bundles. It records timing,
mode/motor counters, target-error proxies, and trajectory divergence. Timing
is local synchronous inference; it does not measure device delivery.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import tempfile
import time
from pathlib import Path

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
PROFILE = ROOT / "examples" / "data" / "human_start.npz"
SEEDS = tuple(range(7, 14))
REPEATS = len(SEEDS)
INTERVALS = {"base32": 32, "variant16": 16, "variant8": 8}
ASSETS = {
    "base32": ROOT / "models" / "continuous",
    "variant16": ROOT / "models" / "continuous-16",
    "variant8": ROOT / "models" / "continuous-8",
}


def sha(path: Path) -> str:
    return hashlib.file_digest(path.open("rb"), "sha256").hexdigest()


def target_receipts():
    import math

    events = {0: (180.0, 100.0)}
    for t in range(1600, 5201, 16):
        phase = (t - 1600) / 1000
        events[t] = (180.0 + 160.0 * math.sin(phase * 1.6),
                     100.0 + 110.0 * math.sin(phase * 2.1))
    events[6400] = (-160.0, 140.0)
    return events


def stats(values):
    a = np.asarray(values, dtype=np.float64)
    if not len(a):
        return {"n": 0}
    return {"n": int(len(a)), "median_us": float(np.median(a) / 1000),
            "p95_us": float(np.percentile(a, 95) / 1000),
            "max_us": float(a.max() / 1000), "mean_us": float(a.mean() / 1000),
            "sum_seconds": float(a.sum() / 1e9)}


def distance_stats(values):
    a = np.asarray(values, dtype=np.float64)
    return {"n": int(len(a)), "median_counts": float(np.median(a)),
            "p95_counts": float(np.percentile(a, 95)),
            "max_counts": float(a.max()), "mean_counts": float(a.mean())}


def make_composed_root(asset_dir: Path, temp_root: Path) -> Path:
    root = temp_root / "bundle"
    root.mkdir(parents=True, exist_ok=True)
    shutil.copytree(asset_dir, root / "continuous")
    shutil.copy2(ROOT / "models" / "renderer_global_h80.bin", root / "renderer_global_h80.bin")
    return root


def build_objects(label: str, interval: int, temp_root: Path):
    from abcurves import ContinuousPipeline, ContinuousPlanner, CountTransform

    custom = label != "base32"
    kwargs = dict(seed=7, prewarm=True, planning_interval_ms=interval,
                  allow_custom_assets=custom)
    movement = ContinuousPlanner(ASSETS[label], **kwargs)
    with np.load(PROFILE, allow_pickle=False) as f:
        profile = np.array(f["profile_hardware"])
        radians = float(f["radians_per_count"])
    composed_root = ROOT / "models"
    if custom:
        composed_root = make_composed_root(ASSETS[label], temp_root)
    composed = ContinuousPipeline(
        profile, transform=CountTransform(radians, y_down=True), seed=7,
        renderer_seed=47007, prewarm=True, planning_interval_ms=interval,
        model_dir=composed_root, allow_custom_assets=custom,
    )
    return movement, composed


def run_trace(obj, interval: int, seed: int, composed: bool):
    import numpy as _np

    policy = obj.movement.planner if composed else obj.planner
    t = time.perf_counter_ns()
    if composed:
        obj.reset(seed=seed, renderer_seed=47000 + seed)
    else:
        obj.reset(seed=seed)
    reset_ns = time.perf_counter_ns() - t
    receipts = target_receipts()
    times, fresh, motor, modes, receipt_ns = [], [], [], [], []
    points, reports = [], []
    active = np.zeros(2, dtype=np.float64)
    errors = []
    mode_transitions = []
    previous_mode = None
    run_started = time.perf_counter_ns()
    for tick in range(8000):
        if tick in receipts:
            active = np.asarray(receipts[tick], dtype=np.float64)
            t = time.perf_counter_ns()
            obj.update_target(receipts[tick], tick * 1000)
            if tick != 0:
                receipt_ns.append(time.perf_counter_ns() - t)
        before = obj.movement.fresh_decisions if composed else obj.fresh_decisions
        before_motor = policy.motor_evaluations
        t = time.perf_counter_ns()
        out = obj.advance((tick + 1) * 1000)
        spent = time.perf_counter_ns() - t
        assert out["xy"].shape == (1, 2) and _np.isfinite(out["xy"]).all()
        times.append(spent)
        is_fresh = (obj.movement.fresh_decisions if composed else obj.fresh_decisions) > before
        fresh.append(is_fresh)
        motor.append(policy.motor_evaluations > before_motor)
        modes.append(policy.mode)
        if previous_mode is not None and policy.mode != previous_mode:
            mode_transitions.append({"at_ms": tick + 1, "from": int(previous_mode), "to": int(policy.mode)})
        previous_mode = policy.mode
        point = out["xy"][0].copy()
        points.append(point)
        errors.append(float(np.linalg.norm(point - active)))
        if composed:
            reports.append(out["reports"][0].copy())
    return {
        "times": np.asarray(times, dtype=np.int64),
        "fresh": np.asarray(fresh, dtype=bool),
        "motor": np.asarray(motor, dtype=bool),
        "modes": np.asarray(modes, dtype=np.int8),
        "points": np.asarray(points, dtype=np.float64),
        "errors": np.asarray(errors, dtype=np.float64),
        "reports": np.asarray(reports, dtype=np.int16) if reports else None,
        "receipt_ns": np.asarray(receipt_ns, dtype=np.int64),
        "reset_ns": int(reset_ns),
        "run_wall_ns": int(time.perf_counter_ns() - run_started),
        "mode_transitions": mode_transitions,
        "motor_evaluations": int(policy.motor_evaluations),
    }


def summarize_runs(runs):
    times = np.concatenate([r["times"] for r in runs])
    fresh = np.concatenate([r["fresh"] for r in runs])
    motor = np.concatenate([r["motor"] for r in runs])
    modes = np.concatenate([r["modes"] for r in runs])
    return {
        "repeats": len(runs),
        "simulated_ms_per_run": 8000,
        "fresh": stats(times[fresh]), "buffered": stats(times[~fresh]),
        "fresh_with_motor": stats(times[fresh & motor]),
        "fresh_without_motor": stats(times[fresh & ~motor]),
        "every_1ms_advance": stats(times),
        "target_receipt": stats(np.concatenate([r["receipt_ns"] for r in runs])),
        "reset": stats(np.asarray([r["reset_ns"] for r in runs])),
        "fresh_decisions": int(np.sum(fresh)),
        "motor_evaluations": int(np.sum(motor)),
        "mode_occupancy": {str(k): int(np.sum(modes == k)) for k in (0, 1, 2)},
        "mode_occupancy_percent": {str(k): float(np.mean(modes == k) * 100) for k in (0, 1, 2)},
        "run_wall_ms": stats(np.asarray([r["run_wall_ns"] for r in runs])),
        "target_error": distance_stats(np.concatenate([r["errors"] for r in runs])),
        "final_target_error": distance_stats(np.asarray([r["errors"][-1] for r in runs])),
        "mode_transitions_per_run": [r["mode_transitions"] for r in runs],
        "motor_evaluations_per_run": [r["motor_evaluations"] for r in runs],
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--keep-samples", action="store_true")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    started = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    with tempfile.TemporaryDirectory(prefix="abcurves-cadence-") as td:
        temp_root = Path(td)
        all_results = {}
        sample_paths = {}
        for label, interval in INTERVALS.items():
            movement, composed = build_objects(label, interval, temp_root / label)
            movement_runs, composed_runs = [], []
            # One discarded trace compiles/exercises the measured path.
            run_trace(movement, interval, 999, False)
            run_trace(composed, interval, 999, True)
            for seed in SEEDS:
                movement_runs.append(run_trace(movement, interval, seed, False))
                composed_runs.append(run_trace(composed, interval, seed, True))
            result = {
                "label": label, "planning_interval_ms": interval,
                "assets": str(ASSETS[label].relative_to(ROOT)),
                "asset_manifest_sha256": sha(ASSETS[label] / "manifest.json"),
                "planner": summarize_runs(movement_runs),
                "composed": summarize_runs(composed_runs),
                "planner_samples": [r["points"] for r in movement_runs],
                "planner_errors": [r["errors"] for r in movement_runs],
                "planner_modes": [r["modes"] for r in movement_runs],
            }
            all_results[label] = result
            if args.keep_samples:
                path = args.output / f"{label}_samples.npz"
                np.savez_compressed(path,
                    points=np.asarray(result["planner_samples"]),
                    errors=np.asarray(result["planner_errors"]),
                    modes=np.asarray(result["planner_modes"]))
                sample_paths[label] = str(path)
            # Release native objects before building the next temporary bundle.
            del movement, composed
    base = all_results["base32"]["planner_samples"]
    for label in ("variant16", "variant8"):
        diffs = []
        endpoints = []
        for base_run, variant_run in zip(base, all_results[label]["planner_samples"]):
            d = np.linalg.norm(np.asarray(variant_run) - np.asarray(base_run), axis=1)
            diffs.append(d)
            endpoints.append(d[-1])
        all_results[label]["vs_base32"] = {
            "trajectory_divergence": distance_stats(np.concatenate(diffs)),
            "endpoint_divergence": distance_stats(np.asarray(endpoints)),
            "per_seed": [{"seed": seed, "rms_counts": float(np.sqrt(np.mean(d * d))),
                          "max_counts": float(d.max()), "endpoint_counts": float(d[-1])}
                         for seed, d in zip(SEEDS, diffs)],
        }
    report = {
        "schema": "abcurves.continuous_cadence_comparison.v1",
        "started_at": started, "finished_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "machine": platform.platform(), "processor": platform.processor(),
        "python": platform.python_version(), "thread_env": {
            k: os.environ.get(k) for k in ("OMP_NUM_THREADS", "MKL_NUM_THREADS", "OPENBLAS_NUM_THREADS", "NUMBA_NUM_THREADS")
        },
        "scope": "same causal 8-second target schedule, native backend, 1 ms output, seeds 7-13",
        "accuracy_note": "Target error and divergence are trajectory proxies, not held-out human-quality labels.",
        "schedule": [{"at_ms": int(t), "target": list(xy)} for t, xy in target_receipts().items()],
        "variants": all_results,
        "sample_paths": sample_paths,
    }
    # Arrays are retained only in optional NPZ files; keep the JSON reviewable.
    for result in report["variants"].values():
        result.pop("planner_samples", None)
        result.pop("planner_errors", None)
        result.pop("planner_modes", None)
    (args.output / "comparison.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf8")
    print(json.dumps({"output": str(args.output / "comparison.json"), "variants": list(all_results)}))


if __name__ == "__main__":
    main()
