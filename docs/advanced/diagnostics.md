# Errors and diagnostics

C returns abc_status and catches native exceptions. `abc_status_message` is static generic text; `abc_last_error` is borrowed per-thread detail (up to 1023 bytes) until the next status call. Success clears that detail. Copy it before queries/cleanup. Python raises `NativeError(status,message,written)` (wrapper type/range checks can raise ValueError/TypeError); Rust returns `Result<T,Error>` carrying status/message/written. Native C++ uses typed exceptions and `PlannerFailure` snapshots.

## Stable C statuses

| Number | Name | Typical cause / response |
|---:|---|---|
|0|ABC_OK|Success; recovery diagnostics can still record deferred input rejection|
|1|ABC_INVALID_ARGUMENT|Invalid values/flags/layout/history; fix input|
|2|ABC_INVALID_HANDLE|Zero/expired/wrong-type token; correct lifecycle|
|3|ABC_NULL_POINTER|Required storage absent|
|4|ABC_INVALID_STATE|Failed planner, forbidden mixed mutation, unready Renderer, invalid session state|
|5|ABC_MODEL_NOT_FOUND|Missing model asset|
|6|ABC_CORRUPT_MODEL|Binary/manifest integrity failure|
|7|ABC_UNSUPPORTED_VERSION|Unavailable ABI/model version|
|8|ABC_ARCHITECTURE_MISMATCH|Unsupported variant/config/tensor architecture|
|9|ABC_TIMESTAMP_RANGE|Negative/backward time, missing relative origin, unordered rendered samples|
|10|ABC_INSUFFICIENT_CAPACITY|Output too short; resize/query capacity and retry|
|11|ABC_ALLOCATION_FAILURE|Allocation failed; inspect failure snapshot if generation began|
|12|ABC_RENDERER_FAILURE|Renderer image/runtime failure|
|13|ABC_INTERNAL_ERROR|Genuine numerical/inference/internal generation failure or native exception|
|14|ABC_RESOURCE_LIMIT|Full bounded pending queue or handle resources exhausted|
|15|ABC_STALE_OBSERVATION|Capture age or retained target-history expiry|
|16|ABC_DUPLICATE_OBSERVATION|Fingerprint or strict equal-capture rejection|
|17|ABC_OBSERVATION_ORDER|Capture/receipt order rejection|

Validation errors normally preserve learned state/RNG and set written=0 when writable. Rejection counters can change. Caller must provide valid aligned pointer storage; the library cannot prove ownership of arbitrary foreign pointers. Error precedence with multiple invalid arguments is not a contract.

## Recoverable pending input

A future relative sum can become nonfinite when its origin appears. Only that record is removed before learned/RNG mutation; valid neighbors and generation continue. Deferred stale input is also recorded individually. Recovery diagnostics contain `pending_observations`, `last_error_status`, `recoverable_observation_errors`, `invalid_pending_observations`, `nonfinite_relative_resolutions`, `stale_pending_observations`, `explicit_pending_clears`, `last_error_capture_us` and `last_error_receipt_us` (plus C size/reserved tags).

`clear_pending_observations()` cancels queued input without resetting the accepted target/time/history/RNG/Renderer. No reset is required solely for malformed deferred input. A failed planner cannot be repaired with pending clear.

## Genuine generation failure

Extreme but finite coordinates can overflow learned features. Genuine failures latch the first cause and block further input/generation until explicit reset. A failed full-segment C call returns written=0 because stack scratch copies only on success. This is not rollback: targets/history, mode, RNG, neural state and sometimes numerical time may already have mutated. Legacy batches/Renderer calls can return a valid partial prefix via written. No fabricated success or automatic reset occurs.

Planner `failed` becomes 1. Later prohibited operations return 4 without replacing the first cause. `abc_segment_get_state` / native `state()` / Python `.state` / Rust `state()?` remains accessible, as do diagnostics/recovery/capacity/last-success metadata (metadata requires a prior success). Counter reset retains the failure snapshot.

`abc_segment_state` reports:

- `failure_status`, `failure_category`, `failure_reason`, `reset_required`, `error_message[256]`.
- `numerical_time_us`, `requested_time_us`, `generation_attempts`, `last_attempt_start_us`, `last_attempt_end_us`.
- `last_segment_id`, `last_segment_start_us`, `last_segment_end_us`.
- `relative_origin_earliest_us`, `relative_origin_latest_us`, `last_origin_error_receipt_us`.

Category: NONE=0, NUMERICAL=1, INFERENCE=2, RESOURCE=3, INTERNAL=4. Reason: NONE=0, NONFINITE_POLICY=1, INVALID_PRIMITIVE=2, INVALID_PROBABILITIES=3, ALLOCATION=4, NATIVE_EXCEPTION=5, UNKNOWN=6. Generation allocation failure uses 11; other genuine failures use 13. Inspect this copied snapshot, then explicitly reset/reinitialize when ready to discard the failed learned session. Renderer reset remains separate.

## Other diagnostics

Segment counters: total segments, fresh decisions, decisions with/without new observation, stale/duplicate/order rejections, unavailable/timeout events, generation errors, accepted/rejected/full-queue input. Gauges: pending count, current availability, last rejection, resolved target/reference, capture/receipt/apply/age times. `last_planning_latency_us` measures local computation. `missed_generation_deadlines` remains 0: the library has no physical scheduler/deadline input.

Planner state includes mode 0 moving/1 braking/2 holding, numerical/requested/decision/target times, decision/motor/sample counts, selected heads, pending samples, failure flag and virtual position. Direct ObservationInput has separate accepted/rejected/duplicate/order/stale counters and resolved-target/reference/availability timestamps. [Every exact field](legacy-api.md#complete-signature-inventory) is generated from the header.

Counters accumulate until diagnostics/session reset. `reset_diagnostics()` clears counters while retaining the last latency, current target/reference/time/pending gauges and recoverable last-error context; it never resets learned state/RNG or clears genuine failure. There is no claim of execution accuracy, device feedback or physical deadline performance in these diagnostics.
