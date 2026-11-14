# ABCurves Python binding

Use `Model`, `Planner`, and `SegmentController` to turn angular target error into movement samples. The ctypes binding has no third-party runtime dependency.

[Install and run Python](../../docs/getting-started/python.md) | [Integration](../../docs/integration.md) | [Examples](../../examples/README.md)

For a source install, build the native DLL first and select it with `ABCURVES_LIBRARY`. Locally built Windows wheels include the DLL and its embedded planner and Renderer models; external model bundles remain optional. No PyPI release is claimed. Package-specific deployment options are in [packaging](../../packaging/README.md).
