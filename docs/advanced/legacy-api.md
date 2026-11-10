# Direct planner operations and the C ABI

Use [SegmentController](../api/overview.md) for normal integration. These supported operations remain available for applications with lower-level needs.

## Direct movement and target APIs

Native `ContinuousPlanner::advance`, C `abc_planner_advance` / `abc_planner_generate_sample`, and Python/Rust planner advance methods take caller-controlled numerical time. Direct target update, loss and position methods operate on that same virtual session. They are not a physical scheduler.

Destroy the attached SegmentController before using these mutation APIs. The library rejects mixed mutation while a controller is attached. Read-only queries and explicit resets/reinitialization remain allowed. See [ownership and reset rules](state-and-memory.md).

## Direct ObservationInput

Native `ObservationInput`, C `abc_observation_*`, Python `ObservationInput`, and Rust's corresponding wrapper process calibrated absolute/relative observations. Unlike SegmentController, this direct handler requires movement to be collected through receipt before submission and does not queue future relative targets. It defaults to STRICT ordering rather than the controller's LATEST. [Observation policies](observations.md) explain validation, duplicates and timestamps.

## Introspection and compatibility

Model/tensor metadata, runtime version/capabilities, layout queries, and planner state snapshots are read-only. C model/tensor queries use `abc_model_get_info` / `abc_model_get_tensor_info`; exact options and fields are declared in the header.

The stable binary contract is **C ABI 1.5**, with 55 exports. Earlier 1.0-1.4 C layouts, symbols and statuses remain compatible. Native C++ class layout is not a stable binary ABI; recompile native C++ consumers. Engine, model format, and reference Python versions are separate.

C options must be initialized with their size-taking initializer, reserved fields zeroed, and output query structures size-tagged. Tokens are library-checked uint64 handles, not memory addresses. Output arrays use element capacities, not bytes. Copy `abc_last_error()` before another status call. [Data and options](data-and-options.md), [errors](diagnostics.md), and [memory ownership](state-and-memory.md) give the detailed supported contract.

## Complete signature inventory

The source headers/bindings are authoritative. To inspect every function, field, pointer direction, unit and default:

```powershell
python API/tools/audit_application_io.py
```

This generates `build/api-reference/symbols.md`, `application_inputs.json`, and `application_outputs.json` from tracked source. No native library is needed for static extraction. After a Release build, add `--probes` to run ABI/default/failure/seed/buffer checks too. `--out <directory>` selects another generated location. Generated inventories are ignored outputs, not another manually maintained reference.

Headers: [C](../../API/c/include/abcurves.h), [C++](../../lib/include/abcurves/native.hpp). Bindings: [Python](../../API/python/src/abcurves_native/__init__.py), [Rust safe](../../API/rust/abcurves/src/lib.rs), [Rust raw FFI](../../API/rust/abcurves-sys/src/lib.rs).
