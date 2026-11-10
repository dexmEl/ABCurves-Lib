# ABCurves documentation

Start with the [Quick Start](quick-start.md), choose your language, then follow the [application integration guide](integration.md).

| Start here | What you will learn |
|---|---|
| [Quick Start](quick-start.md) | Choose a model, submit a target, and get movement in five steps |
| [C++](getting-started/cpp.md) / [C](getting-started/c.md) / [Python](getting-started/python.md) / [Rust](getting-started/rust.md) | Install and run one complete example in your language |
| [Application integration](integration.md) | Required inputs, returned samples, timing, and application responsibilities |
| [Calibration](guides/calibration.md) | Convert pixels to angular counts and movement to device counts |
| [Standard API](api/overview.md) | Model, Planner, SegmentController, samples, reset, and optional diagnostics |
| [Examples](../examples/README.md) | Runnable source and commands |

## When you need more

[Errors](advanced/diagnostics.md) and [troubleshooting](troubleshooting.md) help with common problems. [Advanced features](advanced/README.md) cover the optional Renderer, prefixes, observation policies, resets, buffers, and direct APIs. They are optional reading.

[Performance](guides/performance.md) and [numerical accuracy](guides/numerical-accuracy.md) explain the published comparisons and how to reproduce them.

Building or contributing: [source build](getting-started/building.md), [contributor instructions](../CONTRIBUTING.md), [packaging](../packaging/README.md), and [licensing](licensing.md).

Windows x64 is qualified. Linux/macOS portability is unverified. The stable C ABI is 1.5 and remains compatible with 1.4 consumers; existing advanced APIs remain available.
