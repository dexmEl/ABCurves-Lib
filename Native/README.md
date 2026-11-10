# Preserved upstream reference and Renderer

This directory retains the original ABCurves Python reference, training/export/validation utilities, reference model representations and C99 Renderer. Upstream: https://github.com/optima-manent/ABCurves. Software: [MIT](LICENSE). Dataset terms: [CC BY 4.0](DATASET_LICENSE.md).

The production SDK is in [lib/](../lib/README.md) and [API/](../API/README.md); start at [the root README](../README.md). You do not install this separate `abcurves` Python package to use `abcurves_native`.

`runtime/c/` is a build dependency of the native engine. `models/` contains the optional Renderer and preserved reference representations used by numerical/model-export tests. `abcurves/`, `tests/`, `training/`, `tools/`, `recipes/`, `configs/` and small frozen result receipts support reproducibility. Captures, prepared training arrays, downloads, training runs, upstream presentation assets and large local output were archived locally, preserving all unique resources.

Reference development: `python -m pip install -e "Native[all,dev]"`, then `python -m pytest Native/tests -q`. This is heavier than the production ctypes binding; it requires the original reference assets and optional training/test dependencies. Do not regenerate models during an ordinary SDK build. See [contributor instructions](../CONTRIBUTING.md) and [licensing and attribution](../docs/licensing.md).
