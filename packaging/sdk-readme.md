# ABCurves native SDK

This Windows x64 SDK turns angular target observations into complete movement trajectories. Your application performs calibration, physical scheduling and device communication.

## Start here

1. Select the included model directory (`models/16ms` in the default package; see `PUBLICATION_MANIFEST.json` for other selections).
2. Load the model, create a planner, then create one SegmentController.
3. Submit angular target error X/Y and capture/receipt session timestamps.
4. Request a complete segment and iterate the returned sample count.
5. Convert each 1 ms angular displacement into device counts and schedule output in your application.

Read [application integration](../docs/integration.md), [standard API](../docs/api/overview.md), and [calibration](../docs/guides/calibration.md). Optional features are in [advanced documentation](../docs/advanced/README.md).

## Run a C or C++ example

From this extracted SDK in an MSVC/CMake shell:

```powershell
cmake -S examples/installed -B build/consumer -DCMAKE_PREFIX_PATH="$pwd"
cmake --build build/consumer --config Release
$env:PATH = "$pwd/bin;$env:PATH"
build/consumer/Release/consumer_c.exe models/16ms
build/consumer/Release/consumer_cpp.exe models/16ms
```

Choose an included model path if your package selects a different variant. These examples print numerical samples; they do not control a device. The matching Microsoft Visual C++ runtime is required.

## Contents and compatibility

`include/` provides C/native C++ headers, `lib/` contains libraries and relocatable CMake targets, `bin/` contains the shared library, and `models/` contains selected validated bundles. The optional Renderer image is included only when requested; it additionally needs an application profile and calibrated gain.

C ABI 1.5 retains compatibility with earlier 1.0-1.4 interfaces. Native C++ applications must use a matching toolchain/runtime and recompile. Windows x64 is qualified; no other-platform release is claimed. [Errors](../docs/api/errors.md) explain failures and explicit resets.

Python/Rust bindings and contributor tooling are available from [ABCurves-Lib](https://github.com/dexmEl/ABCurves-Lib); this native SDK does not include the original Python reference, tests, or maintainer archives.

One common angular count is `0.0003509487083280618` radians. Renderer reports, when enabled/calibrated, are already integer device counts: do not convert them twice. Software/models use MIT with preserved LICENSE and NOTICE; dataset-derived material retains its separate attribution. Keep those notices with redistributed bundles.
