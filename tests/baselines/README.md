# Immutable deterministic baselines

Curated from qualified native/API campaigns before repository cleanup. Fixture inputs and numeric expectations were not changed. Expected native CSV output is losslessly gzip-compressed with deterministic headers; compression does not alter numbers. Paths are now stable public test resources rather than raw benchmark result directories.

- legacy/:102 fixtures and reference native movement records.
- observations/:72 original timestamped-observation fixtures and expected records.
- segments/:108 expected segment streams; inputs remain API/tests/step4/fixtures.
- recovery/:12 pending-recovery fixtures/expected records.
- hardening_replay/:27 failure/seed/window/Renderer fixtures/expected records.
- renderer/:reference input profiles/intents for differential checks.
- abi-1.3/:frozen public header, preserving50 original export declarations/151 fields.

Run `python tools/verify.py --regression` from repository root with documented build/dependencies. It checks exact four-language parity and the retained expectations, never reading the private archive. Do not edit expectations to conceal regressions.

Synthetic streams and reference/derived history are used for validation. ABCurves/Optima Manent dataset-derived material retains CC BY4.0 attribution; see root NOTICE and Native/DATASET_LICENSE.md. Software/header portions preserve MIT notices. Original raw campaign files are retained privately, not copied into releases.
