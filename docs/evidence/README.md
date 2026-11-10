# Public measurement evidence

These compact retained summaries support the public [performance](../guides/performance.md) and [numerical accuracy](../guides/numerical-accuracy.md) claims. They are reference data, not required reading for integration.

- [Environment and identities](reference-environment.json): baseline, machine, compiler, source/model hashes.
- [Performance](reference-performance.json): all measured operation/model distributions and speedups.
- [Accuracy](reference-accuracy.json): scenario totals, deviations, RNG/component outcomes and Renderer differences.
- [Model assets](model-assets.json): runtime/reference asset hashes and roles.

Detailed per-fixture traces, component arrays and generated inventories can be reproduced from public source and fixtures using the commands in the guides. Large generated outputs belong under `build/`. Historical internal maintenance receipts are not build dependencies. New measurements do not overwrite these retained results automatically.
