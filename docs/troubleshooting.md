# Troubleshooting

| Symptom | Check |
|---|---|
| DLL/import failure | Match x64 architecture, select ABCURVES_LIBRARY, install matching Visual C++ runtime; DLL beside app or SDK bin on PATH |
| Rust link failure | ABCURVES_LIB_DIR must contain abcurves.lib; runtime PATH is separate; keep both sibling crates |
| Missing model | Pass directory containing both manifest.json and model.abcmodel; do not pass a .pt/.onnx or root models directory |
| Corrupt/incompatible bundle | Validate manifest/hashes and exact supported architecture; do not regenerate weights during installation |
| Status 4 on planner mutation | An exclusive controller may be attached, or genuine failure latched; inspect controller state before resetting |
| Status 9 on relative input | Check earliest/latest retained receipt window and original times; only latest segment origins are retained |
| Status 14 on submit | Bounded queue is full; consume eligible inputs, stop queuing excess future data, or deliberately clear pending |
| Repeated duplicate rejection | Fingerprint ignores receipt; capture/kind/availability/value matching can be retransmission |
| Python reset replay differs | reset() selects 7; reset_session()/controller session reset retains configured seed |
| Zero/one idle samples | No target/no pending commitment produces one 1 ms stationary sample; iterate actual count |
| Old output changes on next call | Reused SampleBuffer/NumPy view was overwritten; copy or use separate consumer storage |
| Renderer not ready | Supply exactly 256 int16 profile reports then reset/begin; calibrate positive gain |
| Genuine status 13 | Copy last error, query first-cause state/diagnostics; generation may be partially mutated and requires explicit reset |
| Selected model missing | Check the SDK manifest or source bundle path; Python wheels keep models external. Copy the chosen bundle with LICENSE and NOTICE |

The library never schedules physical output or catches up to wall time. Generating ahead advances virtual time immediately. A missed detector update retains the target by default. Read [integration](integration.md) and [errors](advanced/diagnostics.md).
