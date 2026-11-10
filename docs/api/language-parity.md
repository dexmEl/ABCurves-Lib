# Language API coverage

All four interfaces call the same C ABI 1.5 implementation in the Windows x64 DLL. "C ABI" means that the feature is available through the C interface rather than a dedicated convenience method. C++ can use `Library::resolve` with the declared C function type and the object's `native_handle()`; Rust can use the sibling `abcurves-sys` crate. These lower-level calls require the caller to respect C ownership and lifetime rules.

| Capability | C | C++ DLL wrapper | Python | Rust safe crate |
|---|---|---|---|---|
| Embedded/external models and tensor information | Supported | Supported | Supported | Supported |
| Planner options, prepared/raw history, state and reinitialization | Supported | Supported | Supported | Supported |
| Relative/absolute targets, loss, controller options | Supported | Supported | Supported | Supported |
| Complete segments, reusable buffers, metadata | Supported | Supported | Supported | Supported |
| Reset, diagnostics, recovery and pending observations | Supported | Supported | Supported | Supported |
| Embedded/external Renderer, profiles, batches and segments | Supported | Supported | Supported | Supported |
| Direct planner stepping and separate observation input | Supported | Supported | Supported | Supported |

The entries marked Supported use each language's normal API (the C API is the
low-level interface). Less common ABI operations remain available to C++ via
`Library::resolve` and to Rust via `abcurves-sys`. No capability in the table
is intentionally unsupported.

The C++ wrapper retains the DLL while dependent objects live. Keep one controller per planner and serialize access to a shared planner, controller or Renderer; independent sessions may run on separate threads. The Rust dynamic loader keeps the DLL loaded for the process lifetime, and caches the first load result. Set `ABCURVES_LIBRARY` before the first API call; after a load failure, correct the path and restart the process. On Windows the default Rust feature uses dynamic loading; use `--no-default-features --features linked` for conventional import-library linking. Other platforms retain linked behavior and are not qualified by the Windows release.

C++ native failures throw `abcurves_dll::Error`, which exposes the ABI status.
Direct planner advancement and Renderer batch methods throw `OutputError`,
whose `written()` identifies the valid output prefix. Segment generation is
transactional and does not expose partial samples on failure. Loader failures
use `std::runtime_error`. The Rust dynamic loader currently reports a generic
message for a missing DLL or export; it does not identify the particular missing export.
