# Build from source

## Requirements

Windows x64 is qualified. Use CMake >=3.20, a C++20 compiler, and C99 support. On Windows install Visual Studio's **Desktop development with C++** workload and a Windows SDK, then use an x64 Developer PowerShell. The tested compiler is MSVC 19.50.35728; this is an executed version, not an invented minimum. Linux/macOS portability remains unverified.

Python >=3.10 is needed only for the Python binding; Rust uses edition 2021 with no declared minimum Rust version. The native runtime does not need Python, NumPy, Numba or Torch. Release DLL deployment needs the matching [Microsoft Visual C++ runtime](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist).

## Get the source and build

```powershell
git clone https://github.com/dexmEl/ABCurves-Lib.git
cd ABCurves-Lib
cmake -S . -B build/api -DABC_PROFILE=OFF
cmake --build build/api --config Release --parallel 4
ctest --test-dir build/api -C Release --output-on-failure
```

`Native/` is ordinary tracked reference/Renderer source; no submodule setup is needed. No training or model export is required. Choose a bundle from `lib/models/{32,16,8}ms`, containing both `manifest.json` and `model.abcmodel`.

The C ABI DLL/import library and examples are in `build/api/Release`. Native probe tools are in `build/api/engine/Release`. `cmake -S API` and `cmake -S lib` remain supported alternate entry points.

## Install and link

```powershell
cmake --install build/api --config Release --prefix build/install
```

Installed CMake targets are `abcurves::native` (C++), `abcurves::c` (C), and `abcurves::renderer_core`. C includes `<abcurves.h>`; C++ includes `<abcurves/native.hpp>`. Use matching x64 Release/Debug configuration and runtime.

CMake installation installs libraries and headers; [SDK staging](../../packaging/README.md) additionally selects models and documentation. Python wheels keep models external. See the language guide for [C++](cpp.md), [C](c.md), [Python](python.md), or [Rust](rust.md), then [Quick Start](../quick-start.md).

A standalone consumer verifies installation without source include/library paths:

```powershell
cmake -S examples/installed -B build/consumer -DCMAKE_PREFIX_PATH="$pwd/build/install"
cmake --build build/consumer --config Release
$env:PATH = "$pwd/build/install/bin;$env:PATH"
build/consumer/Release/consumer_c.exe lib/models/16ms
build/consumer/Release/consumer_cpp.exe lib/models/16ms
```

## Contributor options

To omit tests/examples/benchmarks, configure a separate build with `-DBUILD_TESTING=OFF -DABC_BUILD_TESTS=OFF -DABC_BUILD_EXAMPLES=OFF -DABC_BUILD_BENCHMARKS=OFF`. `ABC_PROFILE=OFF` is required for reported performance; AVX2 runtime dispatch and strict floating point are enabled normally.

[Contributor tests](../../CONTRIBUTING.md) cover Debug, bindings, sanitizers, model validation and deterministic fixtures. [Troubleshooting](../troubleshooting.md) covers loader and model problems.
