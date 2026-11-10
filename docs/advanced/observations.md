# Observations, timestamps and gaps

All session times are nonnegative signed 64 microseconds. Normalize detector/receipt timestamps to one monotonic epoch; numerical time starts 0. No capture FPS is configured, inferred or required. Observation frequency does not change learned 32/16/8 ms decisions.

| Time | Supplied by | Meaning |
|---|---|---|
| Capture | Application | When target information was observed |
| Receipt | Application | When information became available |
| Numerical | Library | Last generated closed 1 ms endpoint |
| Application boundary | Library | Boundary at which eligible pending target is applied |
| Physical output | Application | Its own scheduled/transmitted report time |

Capture<=receipt; default maximum capture-to-receipt age 250000 us (-1 disables). Accepted receipt order cannot decrease. Receipts outside 640 ms retained target history are stale. That limit is separate from the shorter relative-origin window.

## Ordering and duplicates

`STRICT=0`: capture strictly increases; equal capture is duplicate. `LATEST=1` (controller default): capture may be equal for revisions, but cannot decrease. `CALLER=2`: capture order is caller-managed; receipt still cannot decrease. Direct low-level ObservationInput defaults STRICT.

All policies reject matching fingerprints among 64 recent records. Fingerprint = capture, kind, availability and (when available) X/Y; receipt is not included. A retransmission at a newer receipt can still be duplicate. Rejection does not mutate learned state or consume RNG, but diagnostic counters can change.

## Deferred application

A commitment can receive zero, one or many records. Returned output is unchanged. At the next eligible decision, all valid receipt<=boundary records apply in receipt order, preserving intermediate target-history contributions. The last applied state is current. Future receipts remain in bounded storage (default/max 64); full queue returns 14 without displacing accepted records.

Learned history bins receipts into millisecond features; later observations in the same bin may replace its value. Preserving chronological application does not imply an independent feature slot for every event.

## Relative origins

Relative input uses virtual position at receipt, at the closed 1 ms sample at/before a fractional receipt. It does not use physical position or capture-time movement. Exact origins are retained from the last returned segment's start through current numerical boundary, inclusive; before a returned segment only the current boundary is available.

`controller.state` / `abc_segment_get_state` exposes `relative_origin_earliest_us`, `relative_origin_latest_us`, `numerical_time_us`, last segment interval, and `last_origin_error_receipt_us`. Older historical relative receipts return timestamp 9 with missing-origin detail, preserving learned/RNG/pending state. Age/order checks can take precedence when multiple rules fail. Future receipts may queue and resolve when their origin exists.

Just-in-time requests while consuming the latest output fit this retention policy. Pre-generating multiple future segments does not. Never shift timestamps, invent historical origins or silently reinterpret relative input as absolute. Stale relative error has no physical-motion compensation.

## Gaps and loss

No fresh observation retains the accepted target by default. Explicit UNAVAILABLE/INVALIDATED enters original learned loss behavior when eligible, after committed movement finishes. No missed update, loss or stale rejection resets the session. Reacquire with an available target.

Optional inactivity timeout defaults -1 (disabled). When boundary minus last applied receipt exceeds the timeout, a timeout event makes the target unavailable; it never resets the session or schedules/stops physical output.

## Recovery bookkeeping

Future relative coordinates can pass finite validation and overflow only when added to their later origin. The controller removes only that record before target-history/RNG mutation, records original timestamps/status, and applies valid neighbors. Deferred history expiry similarly rejects a specific record. Internal neural failures are never swallowed as bad observations.

Rejected deferred records are removed from duplicate fingerprints; ordering watermarks are recomputed from applied and remaining pending input. `clear_pending_observations()` removes queued records and their fingerprints, retains applied duplicate history/current target, recomputes watermarks and counts each clear (including empty clears). Canceled future receipts therefore do not permanently poison later valid input. Session reset clears the whole input session. Counter reset retains current gauges and first-cause failure.

[Errors](diagnostics.md) · [state/reset](state-and-memory.md) · [integration](../integration.md).
