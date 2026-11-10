# Examples

Choose your language and run one program. [Installation guides](../docs/README.md) explain the library paths. Run commands from the repository root after a Release build.

| Language | Complete source | Command |
|---|---|---|
| C++ | [segments.cpp](../API/cpp/examples/segments.cpp) | `build/api/Release/abc_segment_cpp_example.exe lib/models/16ms` |
| C | [segments.c](../API/c/examples/segments.c) | `build/api/Release/abc_segment_c_example.exe lib/models/16ms` |
| Python | [segments.py](../API/python/examples/segments.py) | `python API/python/examples/segments.py lib/models/16ms` |
| Rust | [segments.rs](../API/rust/abcurves/examples/segments.rs) | `cargo run --manifest-path API/rust/Cargo.toml -p abcurves-native --release --example segments -- lib/models/16ms` |

Each loads a model, creates a planner/controller, submits angular target error and prints valid samples. The programs also show a later target update and a gap. Use any 32/16/8 ms model directory. Timestamps are simulated; printing is not a device scheduler. For the shortest one-segment snippets, see the language guides.

[examples/installed](installed/CMakeLists.txt) is a standalone CMake consumer of an installed C/C++ SDK.

## Advanced examples

One [Python feature program](../API/python/examples/features.py) covers optional behavior without duplicating an application per feature:

| Feature | `--scenario` value |
|---|---|
| Prepared 160-sample prefix | `prepared-prefix` |
| Raw 164-sample prefix | `raw-prefix` |
| Multiple asynchronous observations | `async` |
| Missing updates and explicit target loss | `gap` |
| Pending-input recovery | `recovery` |
| Failure diagnostics | `failure` |
| Reusable buffers | `buffers` |
| Session reset | `reset` |
| Optional original Renderer | `renderer` |

```powershell
python API/python/examples/features.py lib/models/16ms --scenario async
python API/python/examples/features.py lib/models/16ms --scenario renderer --renderer Native/models/renderer_global_h80.bin --gain 0.0003509487083280618
```

The Renderer example uses a clearly labeled synthetic smoke profile, not a hardware calibration recommendation. Reusable buffers must not be overwritten while another consumer needs their data.

Low-level planner/observation examples remain in each language's example directory, and native planner+Renderer examples remain under `lib/examples/`. Use them only with the [advanced direct API rules](../docs/advanced/legacy-api.md); do not mix direct mutations with an attached controller.
