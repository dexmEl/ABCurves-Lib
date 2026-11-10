# Original Python versus native C++ performance

On this Windows x64 machine, the optimized C++ engine generated an active learned commitment **2.55–2.89× as fast as** the original Python runtime. Motor inference itself improved only **1.03–1.06×**: the reference already uses Numba and BLAS. Much of the gain comes from orchestration, history handling, allocation and returning samples. Model loading was slightly slower in C++.

These are newly executed measurements from **2026-11-10**, comparing `Native/abcurves/_continuous/` with direct calls into `lib/`. They are not a comparison between two versions of our C ABI. [Machine-readable measurements](../evidence/reference-performance.json), [environment/source/model identities](../evidence/reference-environment.json), and [numerical accuracy](numerical-accuracy.md) accompany the results.

## Baselines and environment

| Item | Measured configuration |
|---|---|
| Original | Local `abcurves` 2.0.0 Python reference, Numba neural backend and BLAS |
| Native | C++20 engine 0.2.0, current ABI 1.4 sources; **direct static linkage**, no ctypes/FFI |
| CPU / OS | AMD Ryzen 7 5700X3D, 8 cores / 16 logical processors; Windows 11 build 26200 |
| Native build | MSVC 19.50.35728, x64 Release, `/O2 /fp:strict`, runtime AVX2, `ABC_PROFILE=OFF`; no FMA contraction |
| Tools | CMake 4.3.0-rc2; Python 3.13.14; NumPy 2.4.6; Numba 0.66.0; llvmlite 0.48.0 |
| Models | Same 43 tensors per variant; 32, 16 and 8 ms weights verified by SHA-256 |
| Execution | Thread pools set to one; warmed JIT and filesystem; five runs, alternating Python/C++ order |

The upstream base is `e197441f8d343d3b4a4069c7cae20764d0953d15`; the checked-in reference has local changes. The 16/8 ms models are locally trained bundles, used unchanged in both implementations. This campaign does not benchmark a pristine upstream release or its Static Planner.

No CPU affinity, frequency lock, power-plan control or scheduler isolation was applied. Other system activity and timer resolution affect short calls and tails. These measurements are local observations, not hard real-time guarantees or worst-case execution bounds.

## Equivalent work and measurement boundaries

Both implementations use the same target, model, seed and initial movement history for each matched workload. Loading and reset/setup are outside inference timers. Every operation gets 32 warmup iterations before timing. Individual elapsed values use `perf_counter_ns` in Python and `steady_clock` in C++; the observed Windows clock granularity is about 0.1 µs. Timer overhead is not subtracted.

| Operation | Exactly what is measured |
|---|---|
| Load model bundle | Python `Assets` loads/validates the NPZ/ONNX bundle; C++ `Model::load` loads/validates binary + manifest. Same tensors, different formats/validation paths. Warm filesystem; excludes imports/JIT. |
| Initialize, loaded model | Construct planner/runtime from already loaded assets. C++ reuses packed immutable weights while a retained planner keeps them alive; Python creates its normal engine/workspaces. Includes normal allocation/destruction. Excludes prewarm/model load. |
| Reset session | Reset position, history, target/event state, time and seed to the original initialization. |
| Queue target | Legacy absolute target queue update, increasing receipt time. Excludes SegmentController capture/order/reference-window validation. |
| Fresh decision + first sample | Reset outside timer; seed 7, target `(320,120)`, 160 prefix pairs `(0.1,0.05)`. Advance to 1 ms. Both assert exactly one motor evaluation. |
| Complete commitment | Same fresh active state; advance through all 32/16/8 endpoints in one call. Python has no segment API: `MovementRuntime.advance(commitment_us)` reconstructs the equivalent work. C++ uses direct `ContinuousPlanner::advance`, rather than timing controller-only overhead. |
| Buffered sample | Retrieve one already planned 1 ms endpoint, with no fresh decision. No model load or inference. |
| Motor/event history features | Process the same 640 movement/target records, masks, timestamps and event state into neural inputs. |
| Motor inference | Recurrent motor network on the precomputed features. Excludes feature extraction, selector, and trajectory decoding. |
| Selector geometry + network | Decode candidate geometry, construct selector geometry and run the network, with no previous selected head and matching actual movement. Excludes RNG selection. |
| Hazard / brake inference | Same precomputed event context; compute hazards or brake parameters. |
| Decode selected head | Decode head 3 for the selected model's commitment width. |
| Brake path endpoints | Evaluate the same polynomial path at `commitment+1` endpoints. Python includes its ordinary argument-array creation and Numba wrapper; C++ evaluates a scalar loop. This is an equivalent task, not an isolated instruction comparison. |
| Prepare raw prefix | Convert the same 164 sinusoidal displacement pairs and anchor `(23,-7)` into 160 prepared pairs and filtered anchor. |
| Load Renderer image | Load/validate `renderer_global_h80.bin`; reference calls its frozen C99 DLL, native calls the integrated original C99 implementation. |
| Prepare profile | Prepare the same 256 integer reports. |
| Begin Renderer stream | Begin seed 7 using a prepared profile. Python copies its prepared context into a new stream; C++ reuses its object. Native profile preparation is outside this timer. |
| Renderer report | One persistent step from precomputed sinusoidal intents in native-count units. Python includes ctypes/NumPy wrapper cost; C++ calls the C99 core directly. The Renderer math is already native in both. |

Target updates, selected-head decoding and raw-prefix preparation use **16-call batches**; buffered retrieval uses **four-call batches**. Reported values are per-call batch averages. Their p95/p99 describe batch averages, not individual-call tail latency. All other rows measure individual operations. Tiny buffered/queue-call ratios mainly reflect Python wrapper overhead and should not be interpreted as inference speedups.

Five runs provide, per implementation and variant: 100 loads, 1,000 initializations/profile preparations/stream starts, 2,000 fresh decisions and commitments, 2,500 motor inferences, 6,000 buffered/report measurements, and 5,000 measurements for the other stages. Percentiles are NumPy's default linear interpolation over pooled measurements. Speedup is **Python median / C++ median**, calculated before rounding; below 1 means C++ was slower.

The timing paths retain results or check diagnostics so the work is observable. The benchmark does not count allocations or measure physical output deadlines.

## Complete measurements

| Complete commitment | Original Python median | Native C++ median | Speedup |
|---|---:|---:|---:|
| 32 ms | 665.60 us | 230.60 us | 2.89x |
| 16 ms | 615.65 us | 230.10 us | 2.68x |
| 8 ms | 582.70 us | 228.90 us | 2.55x |

The retained [measurement JSON](../evidence/reference-performance.json) includes every operation/model mean, median, p95, p99, maximum and count. Model loading measured slightly slower in native code (32 ms: 7.02 ms Python / 7.73 ms C++); Renderer report timing was 21.40 / 16.80 us, a 1.27x wrapper-level gain. Full function tables are generated locally by the reproduction command below.

## Work without a fair upstream equivalent

The original runtime has no SegmentController, explicit unavailable/invalidation event, relative-origin retention window, failure-snapshot API, bounded asynchronous queue recovery, or exclusive-controller ownership check. These native features are tested separately; no invented upstream speedup is assigned to them. Full history mutation and policy evolution are included in the complete-commitment workload; private Python history methods are not mislabeled as independent public APIs. The non-equivalent Static Planner and training pipeline are outside this comparison.

## Reproduce

Use a separate environment with the reference dependencies; the deployed ctypes binding itself does not need NumPy or Numba. From the repository root on qualified Windows x64:

```powershell
python -m venv Native/.venv
Native/.venv/Scripts/python -m pip install -e Native
cmake -S . -B build/api -DABC_PROFILE=OFF
cmake --build build/api --config Release --parallel 4
cmake -S tools/reference_comparison -B build/reference-tools
cmake --build build/reference-tools --config Release --parallel 4
Native/.venv/Scripts/python tools/reference_comparison/compare.py --benchmark --runs 5 --out build/reference-comparison-final
```

Run no other tests or CPU-heavy programs while timing. The Python runner sets OMP/MKL/OpenBLAS/Numba thread counts before importing the reference. It prewarms each variant. Raw per-operation CSVs, native kernel receipts, prefix output and same-input Renderer reports are written to `build/reference-comparison-final/performance/`; source/executable identities are in `environment.json`. Failed preliminary attempts are separate from this final campaign and are not pooled.

After running the accuracy commands in [numerical accuracy](numerical-accuracy.md), generate the detailed reports and verify all calculations with:

```powershell
Native/.venv/Scripts/python tools/reference_comparison/report.py --results build/reference-comparison-final
```

This verifies all 129 source tensor hashes, fixture/source identities, finite timings, sample counts and arithmetic. Detailed tables, per-fixture/component receipts and JSON reports go to `build/reference-comparison-final/reports/`, not the public documentation. The command does not re-export weights or overwrite retained evidence. Machine-specific timings will naturally differ on another run.

After updating README headline values, execute its code example and check its table arithmetic/calibration against those public receipts:

```powershell
Native/.venv/Scripts/python tools/reference_comparison/verify_docs.py
python tools/check_publication.py
```
