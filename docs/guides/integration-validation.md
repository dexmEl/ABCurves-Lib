# Validate an application integration

Use this guide after following the [integration assistant](../../AGENTS.md), the selected [language guide](../README.md), and the [integration contract](../integration.md). The implementing agent runs builds and tests in the user's project. A documentation MCP server supplies these instructions and examples; it does not run the project or measure hardware.

## 1. Record what is being tested

Record the library/package version, ABI, selected 8/16/32 ms model, language, OS/architecture, installation route and actual runtime/model paths. Confirm the installed declarations match the documentation. Windows x64 is qualified; other platforms need their own verification.

Record input units and axis mapping, output conversion, monotonic clock origin, and calibration settings. Identify measured values and unknowns. Keep physical output disabled while required calibration is incomplete; numerical tests can proceed with explicit synthetic inputs.

## 2. Establish a numerical smoke test

First run a small example without a detector or device transport. Load the selected model, create a persistent Planner and SegmentController, submit a finite relative target with simulated session timestamps, and consume the returned samples. Use the exact APIs in the chosen language guide or installed headers/binding.

For a source checkout, [build the library](../getting-started/building.md) in Release first. From the repository root, these existing examples exercise external model loading and segment generation with the recommended 32 ms starting model:

| Language | Command |
|---|---|
| C | `build/api/Release/abc_segment_c_example.exe lib/models/32ms` |
| C++ | `build/api/Release/abc_segment_cpp_example.exe lib/models/32ms` |
| Python | `.venv/Scripts/python API/python/examples/segments.py lib/models/32ms` |
| Rust | `cargo run --manifest-path API/rust/Cargo.toml -p abcurves-native --release --example segments -- lib/models/32ms` |

Replace `32ms` if you choose a different model directory. For Python, install the runtime binding into that environment and set `ABCURVES_LIBRARY` to the matching DLL as described in the [Python guide](../getting-started/python.md). For Rust, configure DLL discovery as described in the [Rust guide](../getting-started/rust.md). An SDK or wheel consumer should use its installed paths and the embedded-model examples in the language guides; it does not need this repository layout.

These examples establish basic loading and API wiring. Add assertions to the host project's tests for its adapters and execution behavior; a successful example alone does not validate a complete integration.

## 3. Check the planner and controller contract

Use deterministic synthetic observations and a fixed seed. Validate these properties without requiring every sample to move:

- Model loading, construction and observation submission succeed; failures preserve the library's diagnostics.
- Iterate the actual sample count, never the entire backing buffer. The count agrees with metadata and fits the selected model's required capacity. Idle segments and commitment remainders can be shorter than the model interval; an idle segment may contain only one sample.
- Positions and displacements are finite. Holding and zero displacement are valid results.
- Sample timestamps increase by 1,000 microseconds within a segment. A segment's first sample is one millisecond after its start; its last timestamp agrees with its end. Consecutive consumed segments continue the same numerical timeline.
- Capture/receipt times use integer microseconds in one monotonic session domain. Capture is no later than receipt, and receipt order does not decrease. Production timestamps come from the real pipeline rather than reusing the simulated zeros.
- Submitting an observation during a commitment leaves already generated output unchanged. Generate the next segment just before consumption so later observations can affect future movement.
- A missing update retains the last target. Test explicit unavailable input for actual target loss and an available observation for reacquisition. Do not expect loss to cancel an existing commitment or demand an immediate zero sample.
- Returned storage remains valid until consumed. Reuse borrowed buffers only after consumption, serialize access to a session, and release controllers/planners before their parent resources.

Test the host's restart policy separately: coordinate the numerical session, clock, queued output and any Renderer stream. Do not treat repeated resets as a fix for timestamp or scheduling errors.

## 4. Check input and output adapters

Test conversions independently of inference using known vectors from the [calibration guide](calibration.md):

- For pixels, undo crop/resize/padding transforms, use full viewport dimensions and the correct FOV/projection, and verify the reference maps to zero and the signs map to right/up. Check representative off-center points and zoom changes. An off-center reference requires the actual camera geometry.
- For angular input, verify degrees/radians/common-count conversion using `0.0003509487083280618` radians per common count. Device counts require measured device-to-angle calibration first.
- For manual device output, convert **sample displacement** to angle and then through the measured per-axis counts-per-degree gains and signs. Test positive, negative and fractional movements across sample and segment boundaries. Integer accumulation should preserve the requested total to within the adapter's documented rounding remainder, without silent clipping or discarded reports.
- For the optional Renderer, validate the 256 chronological signed int16 X/Y profile reports and positive measured radians-per-native-count gain. Keep stream state across segments and preserve report order. Its output is already integer native counts; do not convert it a second time. A synthetic profile tests execution only.

Prefer a recording output adapter for automated tests. Verify report order, scheduled deadlines, sample-to-report mapping, queue ownership and the host's handling of late or failed sends. A numerical timestamp assertion does not establish real 1 ms delivery.

## 5. Build and test the actual host project

Use the project's own build, formatter/static checks and relevant tests for the changed integration. Add targeted tests for the contracts above where the project has a test harness. Other languages can use the C ABI through an application-owned FFI adapter; validate its layouts, lifetimes, errors and calling convention rather than claiming an official binding.

If changing ABCurves itself, follow [CONTRIBUTING.md](../../CONTRIBUTING.md). Its native, Python and Rust suites test the library. The [runtime package checker](../../tools/test_runtime_package.py) exercises a staged runtime across the four supported languages. Those maintainer checks complement host tests; they do not establish the user's detector mapping, physical calibration or scheduler behavior.

## 6. Verify measured calibration and timing

With the real application/device, repeat signed X/Y calibration measurements under the recorded sensitivity, zoom, acceleration and device settings. Compare expected and observed angular movement. Exercise target loss/reacquisition and capture latency, and inspect actual output timing/late-send measurements. Recalibrate when the applicable response settings change.

Report what passed, exact commands, configuration/code locations and remaining measurements. Distinguish numerical wiring, host tests and measured application/device behavior. An integration with missing calibration or timing evidence remains incomplete in those respects.
