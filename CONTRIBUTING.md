# Contributing

## Setup and repository layout

Follow the [build guide](docs/getting-started/building.md). `lib/` owns the engine/models; `API/` owns interfaces and language tests/examples; `Native/` preserves the original reference, model export resources and C99 Renderer. `tests/baselines/` contains deterministic inputs and retained expectations. Public documentation starts at [docs/README.md](docs/README.md).

Preserve model bytes, strict floating point, RNG mapping, target history, commitment behavior and Renderer math. Do not edit expected movement to conceal a regression. C ABI 1.4 changes must remain compatible: do not resize existing C structures, change signatures, or repurpose statuses. Native C++ consumers recompile.

## Tests

From an MSVC-enabled shell after the source build:

```powershell
python -m venv .venv
.venv/Scripts/python -m pip install -e "API/python[test,numpy]" -e Native
$env:ABCURVES_LIBRARY = "$pwd/build/api/Release/abcurves.dll"
$env:ABCURVES_LIB_DIR = "$pwd/build/api/Release"
$env:PATH = "$pwd/build/api/Release;$env:PATH"
$env:CARGO_TARGET_DIR = "$pwd/build/rust"
.venv/Scripts/python tools/verify.py --regression
```

The runner builds Release and Debug, runs native CTest, Python/generator and Rust tests, live API probes, model checks, and exact legacy/observation/segment/recovery/hardening fixtures. It writes to `build/validation/` and uses public files only. `--resume` is for resuming unchanged sources/configuration, not skipping validation after edits.

Individual checks:

```powershell
ctest --test-dir build/api -C Release --output-on-failure
python -m pytest API/python/tests API/tools/tests -q
cargo test --manifest-path API/rust/Cargo.toml --release
python API/tools/audit_application_io.py --probes
python tools/check_publication.py
```

Original reference tests are optional and have additional training/reference dependencies: install `Native[all,dev]` and run `python tools/verify.py --reference`. Reference Python requires >=3.11; the runtime binding does not need those dependencies.

`python tools/verify.py --asan` configures MSVC AddressSanitizer under RelWithDebInfo, builds the instrumented subset, and runs its 21 tests. The runner locates the MSVC ASan runtime; install the corresponding toolchain support. Ordinary Release/Debug runs 26 tests and longer stress loops. See the explicit targets in [tools/verify.py](tools/verify.py).

## Benchmarks and documentation

```powershell
python tools/benchmark.py --out build/benchmarks/current
python API/tools/audit_application_io.py
python tools/check_publication.py
```

The original-Python comparison has separate [performance](docs/guides/performance.md) and [accuracy](docs/guides/numerical-accuracy.md) reproduction commands. Use Release with `ABC_PROFILE=OFF`; retain compiler/model identities, warmup/counts, raw timings, and median/p95/p99/max. Do not compare a buffered read with fresh inference.

Maintain one beginner explanation per topic: Quick Start, language guide, then Integration. Put supported advanced behavior under `docs/advanced/`. Generate complete signatures/inventories under `build/api-reference/` from headers/bindings; do not commit a second manual symbol list. Examples must compile or execute with the current API.

## Changes and publication

Describe the concrete problem and resulting behavior, relevant test/fixture results, timing limits, and documentation changes. Keep device scheduling/transport in the application. Use existing C++20/C99 and binding conventions. Model export/training is deliberate work, not a cleanup step; preserve licenses and attribution.

Use `build/` for generated results and `.dev-local/` for ignored maintainer material. Ignored-only archives are local resources: back them up separately. Builds, tests and public claims must remain reproducible from tracked source. Gitignore does not untrack existing files or erase history; inspect the index before scoped removals. Never commit environments, credentials, raw captures or machine-specific presets.

[Packaging](packaging/README.md) separates the runtime SDK from public development source. Nothing uploads, publishes or pushes automatically. Preserve [license notices](docs/licensing.md).
