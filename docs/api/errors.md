# Handling errors

Most integration errors come from a missing library/model, invalid target values, timestamps on the wrong clock, or an output buffer that is too small. Read the error before retrying and correct its cause. Errors do not automatically reset the planner.

| Interface | How errors are reported |
|---|---|
| C++ | Exceptions; catch `std::exception` at the application boundary |
| C | `abc_status`; check every status and read `abc_last_error()` before cleanup or another status call |
| Python | `NativeError` with `status` and message; argument validation can also raise `ValueError`/`TypeError` |
| Rust | `Result<T, Error>`; use `?` or handle the error |

For example, Python can catch `NativeError` around observation submission or segment generation and log the exception. Inspect `controller.state` and `controller.diagnostics` when a generation error occurs; both remain available after a genuine failure. Do not keep using partially generated output.

A genuine numerical/inference failure latches its original cause and requires an **explicit** `controller.reset_segment_session()` (Python) before continuing. That deliberately discards the failed session and starts time at zero; it is not automatic recovery. Invalid observations normally leave movement usable after the input is corrected.

Use one controller per planner and serialize access to shared objects. Do not mix its operations with direct planner movement/target writes.

[Common fixes](../troubleshooting.md) are usually enough. The [advanced diagnostics reference](../advanced/diagnostics.md) documents every status, failure snapshot, partial-output rules and recovery operation. [Reset and lifetime details](../advanced/state-and-memory.md) cover intentional session changes.
