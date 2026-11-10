# Build packages locally

For the Windows x64 DLL-only runtime, build Release and stage it with:

```powershell
python tools/publication.py runtime --dll build/api/Release/abcurves.dll --out build/runtime-stage
python API/tools/package_windows.py --dll build/api/Release/abcurves.dll --out build/wheel
python tools/test_runtime_package.py build/runtime-stage --wheel build/wheel/abcurves_native-1.5.0-py3-none-win_amd64.whl
python tools/release_archives.py build/runtime-stage --out build/release-assets --wheel build/wheel/abcurves_native-1.5.0-py3-none-win_amd64.whl
```

The stage includes the DLL, lightweight language interfaces, examples and license
notices, without external models or static libraries. The release directory contains
the minimal and complete SDK ZIPs, the wheel, and a SHA-256 manifest. See the [runtime instructions](runtime-readme.md).
The wheel builder requires `uv`. The validation script below requires Python with `pytest`,
CMake, Rust, `uv`, and an x64 Visual Studio developer shell with `dumpbin` on PATH:

```powershell
powershell -ExecutionPolicy Bypass -File tools/validate_release.ps1 -Python Native/.venv/Scripts/python.exe -Output build/release-validation
```

Choose a new output directory for each run. The script builds and tests the DLL,
checks embedded parity, stages and tests all four consumers, and records DLL exports,
dependencies and package hashes. It does not publish a release.
The developer SDK and source stages remain available below.

The source tree, binary SDK, and local maintenance archive have different purposes. No command here uploads or publishes anything. Preserve the [license notices](../docs/licensing.md).

| Package | Contents | Excludes |
|---|---|---|
| DLL runtime | One DLL, language interfaces and examples, notices | External models, import/static libraries, development fixtures |
| Developer SDK | Headers/libraries, selected external planner models, user/advanced API guides, C/C++ examples, notices | Regression fixtures, Python reference/training, raw results, generated inventories, private archives |
| Public development source | Engine/bindings, build tools, models, reference/export source, tests/baselines, reproducible benchmarks, contributor docs | Builds/environments, caches, local maintenance records |
| Local maintenance archive | Historical reports, full internal receipts, original documentation snapshots and archive manifests | Excluded from Git and every package; needs a separate backup |

## Native SDK

After a Release build:

```powershell
cmake --install build/api --config Release --prefix build/install
python tools/publication.py sdk --install build/install --out build/abcurves-windows-x64
```

This stages the 16 ms planner model by default. Select other bundles with repeatable `--model` arguments; include the optional Renderer image with `--renderer`:

```powershell
python tools/publication.py sdk --install build/install --model 32 --model 16 --model 8 --renderer --out build/abcurves-sdk-all-models
```

The SDK contains `include/`, `lib/`, `bin/`, `models/`, user guides, notices and `examples/installed`. It has no large ABI inventories or maintenance receipts. Links to optional source-only material use the public repository; packaged guides/headers/examples link locally. Copy the package outside the development tree to verify relocation.

Python and Rust are separate language distributions, not automatically included in this C/C++ SDK. Python source wheel: `python -m pip wheel API/python --no-deps -w build/dist`. A DLL-bundled Windows wheel is prepared with `python API/tools/package_windows.py --out build/dist` (requires uv); an ABI 1.5 Windows DLL includes the models. Rust users retain the sibling `API/rust/{abcurves,abcurves-sys}` directories and configure library/runtime paths. No package-registry publication is claimed.

## Public development source

```powershell
python tools/publication.py source --out build/public-source
cmake -S build/public-source -B build/public-source/build/api -DABC_PROFILE=OFF
cmake --build build/public-source/build/api --config Release --parallel 4
ctest --test-dir build/public-source/build/api -C Release --output-on-failure
```

The [manifest policy](publication.json) includes the original Python reference and deterministic fixtures because tests, model export and published comparisons need them. Ordinary binary SDKs do not. Complete API inventories regenerate from tracked source with `python API/tools/audit_application_io.py`; comparison details regenerate using the [performance](../docs/guides/performance.md) and [accuracy](../docs/guides/numerical-accuracy.md) commands.

## Verify contents

```powershell
python tools/check_publication.py
```

Each package gets `PUBLICATION_MANIFEST.json` with file sizes/SHA-256, ABI, model selections and license coverage. Outputs must be new/empty directories under `build/`; staging never recursively cleans earlier results. The legacy `--local-validation-assets` option remains accepted; no assets are currently restricted by that policy.

Source and SDK staging exclude `.dev-local/`, build output, virtual environments, caches and the former maintenance-only documentation paths. `.gitignore` does not remove tracked files by itself: inspect the Git index when archiving material. Ignored-only records cannot be recovered by a fresh clone unless separately backed up. Never include signing keys, credentials or machine-specific presets.
