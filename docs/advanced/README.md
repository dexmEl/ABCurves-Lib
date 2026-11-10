# Advanced features

These references describe supported features beyond the [standard integration](../integration.md). They are optional reading, not prerequisites for basic movement generation.

| Topic | Use it when |
|---|---|
| [Renderer](renderer.md) | You want the original profile-based integer-report output |
| [Initialization, options and data](data-and-options.md) | You need a prepared/raw movement prefix, nondefault settings, or exact sample/metadata fields |
| [Observations and timing](observations.md) | You need ordering policies, target loss/timeouts, deferred input, or relative-reference limits |
| [State, resets, buffers and threads](state-and-memory.md) | You reuse output storage, share models, restart sessions, or need ownership rules |
| [Diagnostics and recovery](diagnostics.md) | You inspect rejected input or a failed generation session |
| [Direct operations and C ABI](legacy-api.md) | You need direct planner stepping, low-level observations, introspection, or exact generated declarations |

Implementations and signatures are unchanged. Use the existing language interfaces; no extra high-level wrapper is required.
