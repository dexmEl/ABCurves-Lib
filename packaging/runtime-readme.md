# ABCurves Windows runtime

This package contains the Windows x64 `abcurves.dll`, all three planner models,
and the Renderer model. Install the Microsoft Visual C++ x64 runtime required by
your DLL build. No ABCurves model files are required at runtime.

**Integrating with an AI agent?** Start with [AGENTS.md](../AGENTS.md) for model
selection, calibration and implementation questions. It includes Codex and Claude
Code setup commands for the hosted [documentation MCP](https://mcp.nanoed.net/abcurves).
The [documentation index](../docs/README.md) and [Quick Start](../docs/quick-start.md)
are included in this SDK for offline use.

* C: `API/c/examples/dll.c` loads the DLL using `LoadLibraryW`; include `API/c/include`.
* C++: `API/cpp/examples/dll.cpp` uses the header-only wrapper. Include
  `API/c/include` and `API/cpp/include`; no ABCurves import library is required.
* Python: install the separately built bundled wheel with `python -m pip install <wheel.whl>`.
  Alternatively install `API/python` with pip and set `ABCURVES_LIBRARY` to this DLL's full path.
* Rust: set `ABCURVES_LIBRARY` to the absolute DLL path and run
  `cargo run --manifest-path API/rust/Cargo.toml -p abcurves-native --example embedded`.

See the [C++](../docs/getting-started/cpp.md), [C](../docs/getting-started/c.md),
[Python](../docs/getting-started/python.md) and [Rust](../docs/getting-started/rust.md)
language guides and the [feature matrix](../docs/api/language-parity.md).

Place the DLL beside the executable for the C and C++ examples. The DLL is not
an operating-system-independent binary: Windows x64 and the appropriate MSVC
runtime are required. For source builds and full guides see the repository.
