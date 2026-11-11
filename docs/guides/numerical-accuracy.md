# Numerical accuracy against the original reference

The [interactive Continuous Planner comparison](../../web/README.md) is a separate campaign using the original showcase’s 18 demo inputs and four seeds, with an explicit matched 1 kHz receipt schedule and unmodified upstream Python. Its 634,800 samples include one differing movement-head choice (H04, seed 101; max position difference 3.7429017638999085 counts). The fixture-corpus bounds on this page do not apply to those demo reruns.

The optimized engine closely follows the original Python Continuous Planner, but **does not produce bit-for-bit identical smooth trajectories in general**. This page reports newly executed comparisons on 2026-11-10, rather than treating exact parity between native language bindings as proof of upstream equality.

Three comparisons serve different purposes:

| Comparison | What it establishes |
|---|---|
| Original Python versus C++ | Agreement of two implementations using identical model weights and deterministic inputs. Small float32 neural reduction differences remain. |
| Earlier native versus current native | Regression compatibility with retained native expectations. |
| C++ / C / Python / Rust bindings | Identical outputs from the same native engine. This does not establish original-Python equality. |

The latter two have separate [qualification evidence](../../CONTRIBUTING.md). This campaign reran the first comparison. [Retained summary results](../evidence/reference-accuracy.json) and [source/environment/model identities](../evidence/reference-environment.json) are public. Per-fixture and component/RNG details are regenerated under `build/reference-comparison-final/reports/` by the reproduction commands below; no private archive is required.

## Reference identity and equivalent inputs

The baseline is the local `abcurves` 2.0.0 reference in `Native/abcurves/_continuous/`, using Numba/BLAS, based on upstream commit `e197441f8d343d3b4a4069c7cae20764d0953d15` with local changes. Native is engine 0.2.0 / ABI 1.4, Release with runtime AVX2 and strict floating point. The [performance guide](performance.md) records the same Windows x64 environment.

All **43 tensors per model** match their native manifest hashes, shapes and dtypes; all three binary bundle hashes and source manifests were verified. The 16/8 ms bundles are locally trained variants, not claimed upstream release weights. Full identities are in the linked receipt and [asset inventory](../evidence/model-assets.json).

| Model | Native `model.abcmodel` SHA-256 |
|---|---|
| 32 ms | `cc38312f2df704fe132068abd930dd6be8c3ea3755fe75ba62d3d3d322ece3f5` |
| 16 ms | `096be81c3a464f3fffe6f3270d091d88d237f8f39b886cbfd972ea38918850ca` |
| 8 ms | `4bb97f154ddf53bec1963ad0d9e9653f561c7f61f5dc1fb50c5dd9dc3ec0dad2` |

We replayed all **102 immutable legacy fixtures**, 34 per model, each lasting 8,000 samples. Coverage includes no prefix and prepared 160-sample prefixes; seeds 7/11/13; 60/120/144/240 Hz simulated target arrivals; static and moving target phases; braking/holding; irregular updates, gaps, late first targets and no-target idle. Inputs are under [tests/baselines/legacy](../../tests/baselines/legacy). Filenames and per-fixture hashes identify exact schedules; this is a finite corpus, not every possible combination.

Both runtimes start from the same virtual anchor/history and accept the same **absolute target receipt schedule**. Capture time equals receipt for this legacy comparison. Each is advanced to every closed 1 ms endpoint; original displacement is reconstructed as current position minus previous position, exactly the native public sample definition. C++ replay CSV uses 17 significant digits for lossless binary64 round-trip. The original runtime has no complete-segment API, so equal policy decision/remainder boundaries establish equal commitments.

For reference tracing, the selected movement head is recovered by uniquely matching its retained selected geometry to the decoded candidate heads; this happens outside performance timing. The original unset idle mode is represented as HOLDING for comparison. No production tracing hooks or model changes were added.

## Smooth movement results

Every sample below is an X/Y pair. **Exact** means both binary64 components compare numerically equal; the separate uint64 representation check gave the same exact counts on this corpus. These percentages measure floating-point equality, not a percentage of movement accuracy. This does not mean the other samples were rounded into equality. No tolerance is used to count exact matches.

All errors below are **common angular counts, per axis**. Maximum is the largest absolute X or Y difference. MAE and RMSE average over all `2 × samples` scalar components; no relative error is used near zero. Position is compared directly; cumulative displacement is also summed independently, so its last digits can differ from position subtraction.

<!-- measured-results:start -->

| Model | Samples | Exact ΔX/ΔY pairs | Exact % | Max displacement error | Mean absolute error | RMSE |
|---|---:|---:|---:|---:|---:|---:|
| 32 ms | 272,000 | 66,969 | 24.62% | 5.34708402e-05 | 5.43954695e-07 | 1.13612729e-06 |
| 16 ms | 272,000 | 72,936 | 26.81% | 7.18393984e-05 | 5.12810047e-07 | 1.19448878e-06 |
| 8 ms | 272,000 | 57,250 | 21.05% | 0.000104953635 | 5.72257079e-07 | 1.39686216e-06 |

| Model | Exact position pairs | Max position error | Position MAE | Position RMSE | Max cumulative Δ deviation |
|---|---:|---:|---:|---:|---:|
| 32 ms | 10,396 | 0.000647211004 | 3.1863876e-05 | 5.6965872e-05 | 0.000647211005 |
| 16 ms | 10,460 | 0.00137361655 | 2.61812389e-05 | 5.45346947e-05 | 0.00137361655 |
| 8 ms | 10,396 | 0.000681142074 | 2.46406494e-05 | 4.98477576e-05 | 0.000681142075 |

<!-- measured-results:end -->

Across these 816,000 legacy samples, all timestamps, decision counts, motor-evaluation counts, modes, selected movement heads, decision timestamps, applied receipt times and pending commitment remainders matched exactly. The maximum per-axis cumulative-position deviation was **0.0013736165527902244 ABCurves common angular counts**, approximately **0.0000276205°** (or **4.83 × 10⁻⁷ radians**). Direct position comparison agrees at the displayed README precision; its unrounded metric is retained in the table. These are observed bounds for this corpus, not arbitrary-input guarantees.

The first divergent displacement/position in the lexicographically first differing fixture for each model was sample 130 (32 ms), 146 (16 ms), and 138 (8 ms), all in the corresponding `120fps_p0_s11_mixed_irregular` case. Indices here are **one-based**, with sample 1 at 1,000 µs. The generated per-fixture JSON records the first difference for every case; there is no single shared divergence index across all scenarios.

### Prefix preparation and additional replays

Nine additional raw-prefix cases (three models × seeds 7/11/13, irregular 120 Hz, 8 seconds each) add **72,000 samples** beyond the tables. Each runtime independently prepares the same 164 pairs and anchor, then receives the same target schedule. Both the dyadic raw pattern and a second sinusoidal preparation produced exactly equal 160-pair histories and anchors; both results are in `raw_prefix` in the public JSON.

These additional replays match timestamps, heads, modes, counters and commitment boundaries. Their maximum per-axis position errors are smaller than the corresponding maxima in the main table. They are reported separately so the README's 272,000 samples per model remain tied to the stable 102-fixture corpus.

### Why the floats differ

The component qualification reran 36 cases (12 per model), tracing features, motor projections/recurrent states, selector inputs/probabilities, hazards, braking and decoding. Input features and activation boundaries designated strict by that tool compare exactly. Neural reductions use different accumulation orders in Numba/BLAS and AVX2 C++; float32 rounding propagates into float64 decoded geometry. All tested non-strict stages meet the existing `atol=2e-4, rtol=3e-5` qualification criteria. Those are **component tolerances**, not a claim that the whole trajectory is exactly equal or bounded by that absolute tolerance.

Five independent RNG cases (seeds 0, 7, 11, `2**32+7`, `2**64-1`) verify exactly equal words, uniform/exponential float values and categorical choices. That tests the original RNG mapping separately from the tested movement-head agreement.

## Renderer: identical input versus independent planners

The optional Renderer uses the original C99 core. We reran 18 persistent streams, six per model, using the retained profiles, gains, seeds and 120 Hz irregular target scenarios. Each stream contains 8,000 reports.

| Model | Same smooth input: exact reports | Independent planners: exact reports | Max cumulative difference | Final totals equal? |
|---|---:|---:|---:|---|
| 32 ms | 48,000 / 48,000 | 48,000 / 48,000 | 0 | Yes |
| 16 ms | 48,000 / 48,000 | 47,993 / 48,000 | 1 device count / axis | Yes |
| 8 ms | 48,000 / 48,000 | 47,998 / 48,000 | 1 device count / axis | Yes |

All **144,000 same-input reports match exactly**. Independently generated planner inputs produce **nine different integer reports out of 144,000**, reproducing the historical result. Cumulative outputs agree again within at most three 1 ms samples, and all 18 streams end with equal totals. Exact mismatch indices, reports and cumulative differences are preserved in the public JSON. These finite-corpus observations are not universal output-error bounds.

Earlier causal tracing located the first difference in planner inference, before Renderer calibration/Q16 quantization. Different earlier intents affect fractional accumulators and recurrent state; the differing report can occur even when that tick's quantized input agrees. The new end-to-end replay reproduces the counts and restoration intervals; it does not repeat every historical internal-state trace. The Renderer math was not changed or compensated. Do not claim smooth-trajectory bitwise equality or universally identical report timing.

## Scope and limitations

Explicit unavailable/invalidation/reacquisition and capture/receipt separation have no direct original-runtime equivalent. Gaps and target updates after gaps are included, but must not be mislabeled as explicit target loss/reacquisition. Native asynchronous controller behavior is covered by separate observation, segment, recovery and hardening fixtures in [qualification](../../CONTRIBUTING.md); it is not scored as an original mathematical mismatch.

The original reference exposes no selected brake-head field, so we compare brake mode/curve components and movement, without inventing a per-tick brake-head comparison. The report does not compare hidden state the reference cannot expose through existing tracing, physical execution, arbitrary hardware settings, all seeds, or other CPU/compiler platforms. Models, RNG policy, expected fixtures and production source are unchanged by this measurement work.

## Reproduce

First follow the reference environment and Release build commands in [performance](performance.md). Then:

```powershell
Native/.venv/Scripts/python tools/reference_comparison/compare.py --parity --out build/reference-comparison-final
Native/.venv/Scripts/python tools/reference_comparison/extended.py --out build/reference-comparison-final
Native/.venv/Scripts/python lib/tests/differential/qualify.py --build build/api/engine/Release --output build/reference-comparison-final/components
Native/.venv/Scripts/python tools/reference_comparison/report.py --results build/reference-comparison-final
```

Raw native CSV, original Python NPZ, per-fixture metrics, Renderer fixtures/reports and component probes remain in the ignored `build/reference-comparison-final/` directory. The public summaries retain numeric results and identities; the tools regenerate all traces from public fixtures alone. `report.py` requires performance and accuracy runs, checks exact diagnostic agreement and component/RNG criteria, then writes detailed JSON and Markdown to `build/reference-comparison-final/reports/`. It does not modify retained public evidence or the guides. No original expected values are overwritten.
