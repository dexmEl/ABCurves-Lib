# Application integration

Use one `SegmentController` to submit target observations and retrieve movement. ABCurves maintains virtual position and movement history; your application handles detection, calibration, timestamps, physical execution, and device communication.

```text
Load model -> Create planner/controller -> Submit angular target
           -> Generate segment -> Convert samples -> Schedule device output
```

## What you provide

| Input | Meaning |
|---|---|
| Model variant | Embedded 32, 16, or 8 ms model; external bundle paths are also supported |
| Target error X/Y | Finite floating-point **common angular counts**, relative to the virtual movement origin at receipt |
| Capture timestamp | When target information was observed; integer microseconds from your session epoch |
| Receipt timestamp | When it became available to your application; the same session clock, no earlier than capture |

Use a single monotonic clock normalized to session time zero. Receipt times must not go backward. Do not pass an absolute OS timestamp into a new session starting at zero or change timestamps to conceal latency. [Pixel and device calibration](guides/calibration.md) explains the units and X-right/Y-up convention.

Submit observations when they arrive; an update is not required for each frame or commitment. Use the relative-target helper for angular error. Absolute virtual targets are also supported in [advanced observations](advanced/observations.md).

Optional setup includes an initial virtual position, RNG seed, initialization prefix, observation policies, and Renderer. Defaults start at virtual `(0,0)`, seed 7, and no external prefix. This virtual origin is not physical mouse position. [Advanced features](advanced/README.md) describe these options when needed.

## What you receive

A request returns one complete committed segment. Normally it contains 32, 16, or 8 samples according to the model; idle output and existing commitment remainders can be shorter. Iterate the actual sample count.

| Sample field | Meaning |
|---|---|
| `timestamp_us` | Numerical endpoint of this 1 ms sample, in session microseconds |
| `displacement.x`, `.y` | Angular movement during this sample, in common counts |
| `position.x`, `.y` | Absolute virtual position, in common counts |

Displacement is relative to the previous sample, including across segment boundaries. A segment's first endpoint is its start time + 1000 microseconds. Metadata gives its start/end time, duration, actual count, model variant, and mode. Holding can produce zero displacement.

## Use the samples

1. Read samples in order.
2. Convert common-count displacement into camera angle, then calibrated device counts for each axis.
3. Preserve fractional remainders when producing integer output.
4. Schedule and transmit reports using your application's timing system.
5. Submit newly received observations and request the next segment just before needed, allowing time for planning.

The optional [Renderer](advanced/renderer.md) can supply calibrated integer reports instead; do not apply the same output conversion twice. Keep output storage intact until its consumer finishes. C/C++ callers supply storage; Python and Rust also offer owned segments.

This numerical example uses simulated arrivals during the first segment. It does not sleep, send device reports, or substitute for a real application clock:

```python
from abcurves_native import Model, Planner, SegmentController

with Model.embedded(16) as model, Planner(model) as planner:
    with SegmentController(planner) as controller:
        controller.relative((120.0, -7.0), 0, 0)
        first = controller.next_segment()
        for sample in first.samples:
            print(sample.timestamp_us, sample.displacement.x, sample.displacement.y)
        receipt = first.metadata.end_timestamp_us - 1000
        controller.relative((12.0, -6.0), receipt, receipt)
        second = controller.next_segment()
```

## Feedback latency and on-target stability

For image-based feedback, consider compensating the angular error for camera movement between capture and receipt. Use measured or estimated executed movement; generated trajectories may run ahead of physical execution. Moving targets may also need a target-motion estimate. Keep the original capture and receipt timestamps truthful. ABCurves uses them for observation timing and validation, but does not automatically compensate capture-time camera movement.

A small angular deadband with hysteresis can reduce repeated corrections caused by detection noise near the aiming reference. Enter the on-target state inside a smaller threshold, and resume pursuit only outside a larger threshold. Tune both thresholds to detector noise, calibration and acceptable aiming error; there is no universal setting. Apply this policy to the application's target estimate and pursuit state.

If the application pauses pursuit, submit an explicit target-unavailable observation; simply withholding updates retains the previous target. Neither a deadband nor target loss cancels movement already generated. These policies can reduce overshoot and small corrections, but do not guarantee an immediate stop. Keep generated segments and planner state consistent with output execution.

## Behavior to rely on

- Observations can arrive asynchronously, including while a segment is being consumed. Serialize access to shared instances.
- A new target cannot interrupt or rewrite already generated movement. It becomes eligible at a later planning boundary; an observation with a future receipt stays future input until numerical generation reaches it.
- Missing updates retain the target by default and do not reset the planner. Explicit target loss is supported, for example `controller.unavailable(capture_us, receipt_us)` in Python; reacquire by submitting a new available target. Existing commitments finish first.
- The planner maintains independent virtual movement history. It does not need physical position, acknowledgments, execution feedback, capture FPS, or ongoing external movement history.
- Generate just in time. Pre-generating far ahead delays observations and may make older relative observations unusable. The controller does not read wall time or catch up automatically after a pause.
- Keep one controller per planner. Start a new session explicitly when your application needs to discard old movement state; do not reset for every missing detector update.

For ordinary use, continue with the [standard API](api/overview.md), [examples](../examples/README.md), and [error handling](advanced/diagnostics.md). Detailed timing policies, reference limits, reset choices, and reusable storage are in [advanced documentation](advanced/README.md).
