# Quick Start

ABCurves plans movement; your application detects targets and executes the output. Start with the installation guide for [C++](getting-started/cpp.md), [C](getting-started/c.md), [Python](getting-started/python.md), or [Rust](getting-started/rust.md). The five steps are the same in each language.

## 1. Choose a model

| Embedded model | Commitment interval | Sample interval | Selection note |
|---|---:|---:|---|
| `32` | 32 ms | 1 ms | Recommended starting point; longest commitment and default comparison baseline. |
| `16` | 16 ms | 1 ms | Middle interval; more frequent planning opportunities than 32 ms. |
| `8` | 8 ms | 1 ms | Shortest commitment; most frequent planning opportunities. |

The interval is how long a movement plan is committed before the planner can make its next decision. All three models return **1 ms samples**. A shorter interval can let a new observation affect a later decision sooner, but it cannot change movement that has already been generated. The interval is independent of camera FPS and device polling rate, and does not guarantee faster execution or more accurate movement. The Windows DLL embeds each model; external model directories are also supported.

Start with the **32 ms baseline** because the maintainer reports stronger human-likeness/AUC results than for the 16 ms and 8 ms variants. Choose a shorter interval when earlier planning decisions are a requirement, then evaluate movement quality in your application. Earlier decisions alone do not make a model a better default.

Quality depends on the exact model bundle and the complete application pipeline. In one user-reported comparison of earlier bundles, the 16 ms and 8 ms models overshot more and changed direction more often than the 32 ms model. Those observations apply to the bundles and test setup used there; they do not establish a universal quality ranking or a result for retrained bundles. A later synthetic replay found the retrained 16 ms bundle had the lowest target error among the compared models, while the retrained 8 ms bundle improved over its earlier version. Synthetic target error does not measure human-likeness; the published evidence does not include a matched human-based AUC evaluation for these retrained bundles. Do not describe a model as universally more natural, smooth, accurate or responsive without a matching evaluation.

## 2. Create the planner

Load a `Model`, create a `Planner`, then create one `SegmentController` for it. The planner maintains its own movement history and virtual position. You can start with the defaults; an initialization movement prefix is optional.

## 3. Submit a target

Give the controller relative angular error X/Y plus capture and receipt timestamps in microseconds. Use **ABCurves common angular counts**, not pixels or device counts. One common count is `0.0003509487083280618` radians. See [calibration](guides/calibration.md) for conversion.

Capture is when the target was observed; receipt is when your application received it. Both use one monotonic session clock starting at zero, with capture no later than receipt. You do not need a target update every frame or every segment.

## 4. Generate movement

Call `next_segment()`. It returns a complete commitment with ordered 1 ms samples; no duration or camera FPS parameter is needed. Use the actual returned count: idle segments can be shorter.

## 5. Use the output

Read each sample's timestamp and X/Y displacement. Convert common angular counts into calibrated device counts, retain fractional remainders, and schedule the movement in your application.

This complete Python example uses simulated timestamps and prints movement without controlling a device:

```python
from abcurves_native import Model, Planner, SegmentController

with Model.embedded(32) as model, Planner(model) as planner:
    with SegmentController(planner) as controller:
        controller.relative((120.0, -7.0), 0, 0)
        segment = controller.next_segment()
        for sample in segment.samples:
            print(sample.timestamp_us, sample.displacement.x, sample.displacement.y)
```

Request subsequent segments just before they are needed. New observations cannot rewrite movement already generated. A holding decision may return stationary samples.

Continue with [application integration](integration.md), or run the [example for your language](../examples/README.md).
