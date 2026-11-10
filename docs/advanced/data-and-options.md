# What applications provide and receive

This optional reference lists exact inputs, defaults, units, buffers and metadata. For the ordinary workflow see [application integration](../integration.md). Generate complete signatures and pointer directions using the [local inventory command](legacy-api.md#complete-signature-inventory).

## Startup inputs

| Input | Required? | Default | Type/validation |
|---|---|---|---|
| Native library | C/C++/Python/Rust; C++ may also link static | Explicit/binding discovery | Same-architecture ABI 1.5 runtime for embedded models |
| Model directory | Yes | None | Both manifest.json and model.abcmodel |
| Model variant | Derived | Loaded bundle |32/16/8 ms, no capture FPS |
| Initial position | Optional | `(0,0)` | Finite float64 virtual common-count X/Y |
| Planner seed | Optional in options |7 | uint64; low 32 affects original RNG |
| Prepared history | Optional | Empty | Exactly 160 finite chronological 1 ms displacement pairs |
| Raw history | Alternative prefix path | None | Exactly 164 finite pairs + explicit finite anchor; prepare to 160 and filtered anchor |
| Diagnostic proposals | Optional advanced | Off | Native diagnostics / C flags bit 1; may add inference work |
| Controller options | Optional | See below | Supplied at construction; replacement controller to change |

Initial velocity/history is derived internally. Targets are conditional: without one the planner can idle. Never supply physical mouse position, FPS, acknowledgments, execution feedback or ongoing external movement history.

## Observation inputs

| Field | Type/units | Rule |
|---|---|---|
| `value.x`, `value.y` | float64 common counts | Finite if available; relative error or absolute virtual target |
| `kind` | ABSOLUTE=0 / RELATIVE=1 | Mutually exclusive coordinate interpretation |
| `availability` | AVAILABLE=0 / UNAVAILABLE=1 / INVALIDATED=2 | Coordinates ignored when not available |
| `capture_time_us` | nonnegative int64 microseconds | Actual capture/source time <=receipt |
| `receipt_time_us` | nonnegative int64 microseconds | Actual availability on same session clock; never decreasing |
| Apply time / reference | Internally derived | Eligible decision boundary / virtual origin at receipt |

C requires size-tagged `abc_target_observation` and reserved=0. Python helpers construct it; `TargetObservation` exposes the same values. No logical apply-time argument is required.

## Optional controller configuration

| Field / Python keyword | Default | Range/effect |
|---|---|---|
| `ordering` | latest / LATEST=1 | STRICT=0, LATEST=1, CALLER=2; capture revision rules; never receipt reordering |
| `max_age_us` |250000 | int64 >=-1; -1 disables capture-to-receipt age limit |
| `inactivity_timeout_us` | -1 | int64 >=-1; -1 disables; timeout makes target unavailable at boundary |
| `observation_capacity` |64 | uint32 1..64; full returns 14 |

Fingerprint history has a fixed 64-record bound independently of queue capacity. Ordinary applications generally keep defaults. [Full ordering/timestamp policy](observations.md).

## Requesting output

| Interface | Operation | Storage/result |
|---|---|---|
| C++ | `next_segment(span<Sample>, SegmentMetadata&)` | Caller span >=`required_capacity()`; returns size_t |
| C | `abc_segment_next(controller,output,capacity,&written,&metadata)` | Caller array, uint64 count/capacity, size-tagged metadata |
| Python | `next_segment()` / `next_segment_into(SampleBuffer)` | Owned Segment / metadata with reused storage |
| Rust | `next_segment()` / `next_segment_into(&mut [Sample])` | Owned Segment / `(usize,SegmentMetadata)` |

Required capacity is model-specific 32/16/8;32 covers all current variants. Actual count can be smaller. No requested duration, generation flag, clock or sample count is needed. Capacity uses elements, not bytes. There is no unbounded movement queue.

## Smooth samples

| Exact field | Type | Meaning |
|---|---|---|
| `timestamp_us` | int64 | Closed 1 ms numerical endpoint |
| `position.x`, `.y` | double | Absolute independent virtual position, common counts |
| `displacement.x`, `.y` | double | This position minus preceding sample position; first uses segment initial position |

One common count is **0.0003509487083280618 radians**. A segment spans `(start_timestamp_us,end_timestamp_us]`; first sample=start+1000, last=end, count=duration_us/1000. No endpoint feedback is needed. Samples have no per-sample mode/validity field: only the written prefix is valid.

## Segment metadata

| Fields of abc_segment_metadata / native SegmentMetadata | Meaning |
|---|---|
| `segment_id`, `variant_ms`, `sample_count` | ID starting 1 per session, nominal model width, actual returned count |
| `start_timestamp_us`, `end_timestamp_us`, `duration_us` | Boundary interval and actual duration, microseconds |
| `first_sample_timestamp_us`, `last_sample_timestamp_us` | Included endpoint range |
| `initial_position`, `final_position` | Float64 common-count virtual endpoints |
| `mode` | MOVING=0, BRAKING=1, HOLDING=2; final mode |
| `fresh_planning_decision`, `motor_inference` | Fresh policy decision / motor evaluated in this call |
| `selected_head`, `selected_brake_head` | Last selected heads; -1 when none |

C adds `struct_size`/reserved fields and uses uint32 flags; native uses bool. `variant_ms` gives nominal width; there is no separate nominal-duration field. Idle 1 and existing-commitment remainders are valid. Querying metadata before a successful segment is invalid state.

## Planner Only and Planner + Renderer

Planner Only returns smooth angular geometry for application conversion. The optional Renderer additionally needs a separate image, exactly 256 chronological int16 native-count profile reports, independent seed, and positive calibrated radians-per-native-count gain. It returns one `abc_report {int16_t dx,dy}` per sample. Reports contain no timestamps; match by index.

Renderer output is already native counts for that calibration, with internal learned texture/quantization and residual state. It is not universally calibrated for arbitrary hardware. Its Q16/int16 range is bounded; applications must honor device limits/signs. [Renderer details](renderer.md).

## Other outputs

- Model/tensor metadata, SHA256 identities, runtime version/layout/capabilities.
- Planner state and segment/observation counters, pending count and target/reference/application timestamps.
- Recoverable rejection counters and original error times/status.
- Failure/window snapshot, explicit reset requirement and bounded first-error text.
- C statuses, thread-local details, Python exceptions or Rust Results.

Snapshots are caller-owned. `abc_last_error` text is borrowed until the next status call. [Diagnostics/errors](diagnostics.md) · [memory/lifetime](state-and-memory.md).
