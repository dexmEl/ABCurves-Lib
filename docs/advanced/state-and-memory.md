# State, reset, ownership and threads

## Exclusive controller domain

A planner permits one SegmentController. While attached, direct position/target/loss, legacy advance/sample generation and direct ObservationInput submission are rejected before mutation. Second-controller attachment fails. Read-only snapshots and explicit reset/reinitialize remain allowed. Destroying/closing/dropping the controller releases the domain without resetting planner state/time; direct APIs can resume subject to their ordinary closed-boundary/queue rules.

Native controllers and ObservationInput borrow the planner and detect expiration with weak lifetime tokens. C validates parent tokens and permits safe orphan destruction. Normally destroy children before parents. Python retains its parent and shared RLock. Rust safe wrappers borrow `&mut Planner`.

Serialize same-instance calls and destruction. Ownership checks are not arbitrary concurrent safety. Independent planners have separate mutable/RNG state and may run on separate threads; immutable Model weights can be shared.

## Reset choices

| Intent | C++ | C | Python | Rust |
|---|---|---|---|---|
| Keep stored seed/prefix | `planner.reset()` | `abc_planner_reset_session` | `planner.reset_session()` | `planner.reset_session()?` |
| Select seed | `planner.reset(seed)` | `abc_planner_reset(planner,seed)` | `planner.reset(seed)` | `planner.reset(seed)?` |
| Controller session | `reset_segment_session()` | `abc_segment_reset_session` | `reset_segment_session()` | `reset_segment_session()?` |
| Replace initialization | `planner.reinitialize(options)` | `abc_planner_reinitialize` | `planner.reinitialize(...)` | `planner.reinitialize(options)?` |

Existing Python `planner.reset()` still selects seed 7. C/Rust seed-changing reset requires an argument. Controller reset retains seed/prefix in all languages. Planner uint64 seeds intentionally map through low 32 bits; seed 11 and 11+2^32 match. Do not assume the Renderer has the same mapping.

Full reset returns numerical/requested time to 0; clears targets, histories, learned state, pending/fingerprints, IDs, counters and failure; reinitializes RNG; and retains configured origin/prefix unless reinitialize replaces them. Parent reset synchronizes controller lazily by revision. Renderer state/seed is separate and untouched. Reset it explicitly for a restarted rendered stream.

Pending-only clear retains learned state/current target/time/RNG. Diagnostics reset clears counters but retains current gauges and first-cause failure. Neither repairs a genuine failed planner.

## Output memory

C/native callers allocate/free sample/report arrays. Capacity is elements, storage must be aligned/nonoverlapping, and only the written prefix is valid. C queries require `struct_size=sizeof(out)`. Prefix/profile data is copied; paths are consumed during the call. No output-buffer pointer is retained. Planners retain immutable weights after releasing the original model token. Destroy native tokens before unloading the DLL.

Native spans/C arrays plus metadata are caller snapshots. They survive later calls/controller destruction until overwritten or freed; multiple segments coexist in different buffers.

Python SampleBuffer owns a contiguous ctypes array. Its optional `numpy()` view retains backing storage and is zero-copy. `next_segment()` creates owned storage; `next_segment_into(buffer)` returns metadata and reuses storage. Old views/Segment objects sharing that buffer see later writes. Copy or use separate storage while another consumer needs prior output. A retained object does not make reused data immutable.

Rust owned Segment contains `[Sample;32]` and actual length; `samples()` borrows only the valid prefix. `next_segment_into` writes a caller slice and returns count/metadata. Owned segments can outlive the controller. Rust borrowing controls slice lifetime; RAII releases tokens.

`abc_status_message` is static borrowed text. `abc_last_error` is thread-local until the next status-returning call; copy before queries/cleanup. Failure snapshots copy a bounded 256-byte message and stay useful after querying diagnostics. Genuine generation may mutate state before failing: no rollback is promised. [Failure handling](diagnostics.md).
