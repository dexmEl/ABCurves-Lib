# Rust

## Requirements and installation

Use the safe `abcurves-native` crate under `API/rust/abcurves`. It uses edition 2021; no minimum Rust version is declared. Windows x64/MSVC with Rust 1.98.0 has been exercised. No crates.io release is claimed.

Use the DLL from the runtime SDK, or [build it from source](building.md). Add a local dependency, adjusting the path for your application:

```toml
[dependencies]
abcurves-native = { path = "../ABCurves-Lib/API/rust/abcurves" }
```

Keep its sibling `abcurves-sys` directory; the safe crate handles FFI details. From the source repository root configure linking/runtime discovery:

```powershell
$env:ABCURVES_LIBRARY = "$pwd/build/api/Release/abcurves.dll"
$env:CARGO_TARGET_DIR = "$pwd/build/rust"
```

Windows uses dynamic loading by default, without an ABCurves import library. Place the DLL beside the executable or set `ABCURVES_LIBRARY` before the first API call. Match the x64 MSVC target and install the matching Microsoft Visual C++ runtime. The DLL embeds all models. `Model::load(path)` remains available for external bundles. For import-library linking use `default-features = false, features = ["linked"]` and configure `ABCURVES_LIB_DIR`.

## Load, submit and generate

Save as your application's `src/main.rs`:

```rust
use abcurves_native::*;

fn main() -> Result<()> {
    let model = Model::embedded(16)?;
    let mut planner = Planner::new(&model, PlannerOptions::default())?;
    let mut controller = SegmentController::new(&mut planner, SegmentOptions::default())?;
    controller.relative(Vec2 { x: 120.0, y: -7.0 }, 0, 0)?;
    let segment = controller.next_segment()?;
    for sample in segment.samples() {
        println!("{} {} {}", sample.timestamp_us, sample.displacement.x, sample.displacement.y);
    }
    Ok(())
}
```

This uses simulated capture/receipt timestamps and returns one complete segment. Iterate `samples()` for the actual valid sample slice. `?` propagates errors; RAII cleans up objects. The application converts angular displacement and schedules device output.

Run the [embedded example](../../API/rust/abcurves/examples/embedded.rs):

```powershell
cargo run --manifest-path API/rust/Cargo.toml -p abcurves-native --release --example embedded
```

Continue with [integration](../integration.md), [calibration](../guides/calibration.md), and [errors](../advanced/diagnostics.md). Read [advanced buffer/reset/ownership details](../advanced/state-and-memory.md) only when needed.
