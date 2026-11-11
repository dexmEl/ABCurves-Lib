# Continuous Planner comparison site

A copy of the original ABCurves showcase’s **Continuous Planner view only**, adapted to compare the original Python planner with ABCurves-Lib side by side. All 18 examples and their four published planner seeds are included. Both views share playback time, camera, pan, zoom, target guide and prefix. The page adds per-draw error statistics, a signed error chart, millisecond scrubbing, a sample inspector, CSV downloads and source provenance.

## Build and preview

Python 3.10+ and its standard library are enough to verify the retained runs and build the website. No model inference, packages or network access are needed for this step.

```powershell
python web/build.py
python -m http.server 8765 --bind 127.0.0.1 --directory build/web
```

Open `http://127.0.0.1:8765/`. To link a particular draw, use `#continuous/H04?seed=101`. Serve through HTTP; the browser loads each example on demand and verifies its decoded SHA-256. Use a current browser supporting Canvas, Pointer Events, native dialogs and `DecompressionStream`.

`python web/build.py --output build/site` selects another output folder. Before writing the site, the builder verifies every original payload against its upstream hashes, checks the native source/model identities, and independently recomputes all numeric metrics and timing/decision checks from the retained full-precision traces. Output is about 28 MB, with only the selected example loaded at startup. Generated HTML and preview artifacts stay in ignored `build/`.

## What is being compared

The original website stores frozen integer Renderer outputs. Its complete original receipt schedules, raw prefix bins and Renderer profiles are not included. Consequently, these are **new matched smooth-planner runs on its published demo inputs**, not a claim to reproduce the old rendered animations.

- Baseline: unmodified upstream Python at commit `e197441f8d343d3b4a4069c7cae20764d0953d15`, using its native Numba/BLAS backend.
- Candidate: the compiled C++ engine through the public C ABI/Python binding, with the matching 32 ms model.
- Recorded examples: difference the published 161 binary64 prefix positions once and provide those same 160 displacements and initial anchor to both planners. No W5 prefix filtering is applied.
- Designed targets: start from rest with zero history.
- Receipts: published `target[k]` at `k * 1000` µs, for `k = 0 .. duration-1`. Queued timestamps are identical; the runtimes do not interpolate future targets.
- Seeds: 7, 23, 101 and 997 for every example. Output is compared at every closed 1 ms endpoint.
- Outputs: original and native positions, reconstructed per-tick displacement, and eight diagnostics. Binary64 positions and integer-valued diagnostic traces are retained losslessly in zlib blocks.

The original target path is displayed as a guide. This comparison uses a defined 1 kHz receipt schedule, not a claim about the original animation’s receipt cadence. These curated examples are a finite corpus, not a held-out movement-quality benchmark or a universal error bound.

## Measured results

The retained run contains **634,800 paired samples across 72 runs**. All timestamps, decision counts, motor counts, modes, decision boundaries, applied receipt times and commitment remainders agree. Movement-head choices agree in **71 of 72 runs**.

**H04, seed 101** chooses a different head starting at the 3,425 ms endpoint. Its maximum per-axis position difference is **3.7429017638999085 common angular counts**, approximately **0.07526182°**; maximum displacement difference is **0.20040201089099696 counts per 1 ms**. The site exposes this case directly. The largest position difference in the other 71 runs is **0.0003422202657930029 counts**. No differing cases are excluded from aggregate metrics.

These results are separate from the [legacy fixture campaign](../docs/guides/numerical-accuracy.md). Do not apply that campaign’s smaller observed bounds to this corpus. Full metrics, per-run first differences, source/model hashes and environment details are in [data/comparison.json](data/comparison.json).

## Regenerate inference results

Use the reference dependencies described in [Native/pyproject.toml](../Native/pyproject.toml), including NumPy, Numba and SciPy. Build a fresh strict-floating-point Release library, then run the generator with that library:

```powershell
cmake -S . -B build/web-native -DABC_BUILD_TESTS=OFF -DABC_BUILD_EXAMPLES=OFF -DABC_BUILD_BENCHMARKS=OFF -DABC_PROFILE=OFF
cmake --build build/web-native --config Release --target abcurves --parallel 4
Native/.venv/Scripts/python web/generate.py --library build/web-native/Release/abcurves.dll
python web/build.py
```

The generator downloads hash-pinned upstream source and model files into `build/upstream-showcase/` if absent. `--reference another/cache` selects another cache. It verifies every file against [data/reference.json](data/reference.json) and uses that unmodified source instead of the locally extended `Native/` Python implementation. The native planner sources and weights are never edited. The generator intentionally retains mismatches; a changed numerical result requires reviewing and rebuilding the public evidence.

## Browser verification

With the preview server running, install the optional browser tooling into ignored build output:

```powershell
npm install --prefix build/web-qa --no-package-lock playwright
build/web-qa/node_modules/.bin/playwright install chromium
node web/qa/verify.cjs
```

This checks all 72 draws against their raw binary64 samples, the exposed head divergence, sample scrubbing, CSV export, gallery and deep links, playback, camera gestures, and two visible panes at 390 px and 320 px widths. Its result is written to `build/web-verification.json`. It also accepts another preview URL as an argument.

## GitHub Pages

The repository includes a manually triggered `Continuous Planner comparison Pages` workflow. After these files are pushed, select **GitHub Actions** as the Pages source in repository settings, then run that workflow from Actions. It verifies and builds the frozen data on Linux and deploys the result. Inference remains an explicit local regeneration step. Local build and preview commands do not publish anything.

For `dexmEl/ABCurves-Lib`, the project URL will be **https://dexmEl.github.io/ABCurves-Lib/**. The root README includes this link. Enable deployment at [Settings → Pages](https://github.com/dexmEl/ABCurves-Lib/settings/pages), then open [the Pages workflow](https://github.com/dexmEl/ABCurves-Lib/actions/workflows/pages.yml) and choose **Run workflow** on `main`. After deployment succeeds, you can also put the URL in the repository’s **About → Website** field.

## Sources and licenses

The camera, fading trail and navigation approach derives from [the original showcase](https://github.com/optima-manent/ABCurves/tree/e197441f8d343d3b4a4069c7cae20764d0953d15/web). The continuous subset retains the original compressed demo blocks and per-example integrity/provenance hashes. Static, Renderer and GIF assets are omitted. Software/model material is MIT; dataset-derived examples retain CC BY 4.0 attribution. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
