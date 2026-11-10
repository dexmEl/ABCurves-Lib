# Optional original Renderer

Planner Only is appropriate when the application already converts angular displacements into device units. The optional Renderer adds learned profile texture and integer quantization to a persistent stream. It does not detect devices, calibrate hardware, schedule reports or communicate with them.

## Inputs and lifecycle

1. Create the embedded Renderer with `abc_renderer_create_embedded`, `Renderer.embedded()` in Python, or `Renderer::embedded()` in Rust. External Renderer model files can still be loaded when needed.
2. Supply exactly 256 chronological signed 16 X/Y native-count profile reports. C++ uses `observe_profile(vector<Report>)`; C/Python/Rust use set-profile equivalents.
3. Begin/reset with an event seed (Python default 7; native `begin(seed)` is explicit).
4. Keep that Renderer alive across segments; render each smooth sample once, in order.
5. Supply application-calibrated, finite positive **radians per native count** gain. Composition scales common displacement by `COMMON_RADIANS_PER_COUNT/gain` without an automatic axis flip. The SDK profile, intent and returned reports use X right/Y up; applications convert device axes explicitly.

Profile signs/scale must match this X-right/Y-up convention; native device Y-down reports must be converted by the application. A synthetic profile only verifies execution; it is not a calibration recommendation.

C `abc_segment_render`, native `SegmentController::render_segment`, Python `Renderer.render_segment` and Rust `Renderer::render_segment_into` use the original adapter. Samples, metadata including initial position, gain and appropriate output storage are required. Output is one `Report {int16 dx,dy}` per smooth sample. No embedded report timestamps: match by index. Reports are native counts, not common angular counts.

The original Renderer retains Q16 intent, quantization and fractional/stream state. Int16 range is -32768..32767; devices can have narrower limits. Unsupported Q16 input produces an error. No universal saturation/device conversion is implied. Caller capacity and partial-runtime-error handling remain its responsibility.

## Executable smoke example

```powershell
python API/python/examples/features.py lib/models/16ms --scenario renderer --renderer Native/models/renderer_global_h80.bin --gain 0.0003509487083280618
```

This deliberately uses a synthetic profile/equal common-native scale. Keep the Renderer across normal segments. Planner reset/clear/counter operations do not reset it. [Errors](diagnostics.md) · [ownership](state-and-memory.md) · [licensing](../licensing.md).
