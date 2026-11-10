# Standard API

The normal integration uses three objects: load a **Model**, create a **Planner**, then use a **SegmentController** for both targets and movement. Start with your language guide: [C++](../getting-started/cpp.md), [C](../getting-started/c.md), [Python](../getting-started/python.md), or [Rust](../getting-started/rust.md).

| Concept | Purpose | What you provide / receive |
|---|---|---|
| Model | Loads a learned planner variant once | Embedded variant or bundle directory -> reusable model |
| Planner | Keeps virtual position, movement history, and planning state | Model, optional initialization -> independent planning session |
| SegmentController | Accepts observations and returns complete movement | One planner -> its standard input/output interface |
| Submit target | Supplies new target information when available | Angular error X/Y and capture/receipt microseconds |
| Next segment | Generates the next committed trajectory | No requested duration -> ordered samples and metadata |
| Sample | Describes one 1 ms interval | Timestamp, displacement, and absolute virtual position |
| Reset | Explicitly starts a new planning session | Retains configured initialization/seed; clears session movement and target state |
| Diagnostics | Optional troubleshooting information | Read-only counters/state; no movement generation |

## Load and create

In Python: `Model.embedded(16)`, `Planner(model)`, then `SegmentController(planner)`. Other languages use the corresponding constructors in their guides. External model paths remain supported. Keep the planner alive while its controller is in use, and close/destroy the controller first. One controller owns target/movement operations for that planner.

The selected variant determines 32/16/8 ms commitment behavior. Do not configure camera FPS. Defaults are sufficient to start; [prefixes and other options](../advanced/README.md) are optional.

## Submit a target

Python: `controller.relative((x, y), capture_us, receipt_us)`.

Call when an observation arrives. X/Y are common angular counts; times use one monotonic session clock. Submission returns no movement. It does not rewrite an existing segment. There is no requirement to submit once per segment. [Integration and units](../integration.md) explains the application contract.

## Get movement

Python: `segment = controller.next_segment()`, then iterate `segment.samples`. Read `sample.timestamp_us`, `sample.displacement.x/y`, and optionally `sample.position.x/y`. `segment.metadata` gives the returned count and time interval.

C++ uses `next_segment_into(storage, metadata)` and the returned count; C uses `abc_segment_next`; Rust uses `controller.next_segment()?` and `segment.samples()`. See the [complete examples](../../examples/README.md) for storage and error handling in your language.

Request a segment just before it is needed. The application converts and schedules its samples. ABCurves does not execute device movement.

## Reset and troubleshoot

Python: `controller.reset_segment_session()`. Use it for an intentional new session; its timestamp epoch starts at zero again. It retains the configured seed/prefix. It is not a response to an ordinary missing observation, and it does not reset an optional Renderer.

Python: `diagnostics = controller.diagnostics`. Read this when troubleshooting; normal generation does not require querying it. [Errors](../advanced/diagnostics.md) explains exceptions/statuses and when a failed planner needs explicit reset.

## Advanced reference

[Advanced API](../advanced/README.md) covers Renderer, initialization prefixes, observation policies, reusable buffers, detailed diagnostics, thread safety, direct planner operations, and C ABI compatibility. All existing public functions remain supported. Exact declarations live in the [C header](../../API/c/include/abcurves.h), [native headers](../../lib/include/abcurves/native.hpp), and binding source; a complete inventory can be [generated locally](../advanced/legacy-api.md#complete-signature-inventory).

See the [language feature matrix](language-parity.md) for wrapper coverage and lower-level alternatives.
