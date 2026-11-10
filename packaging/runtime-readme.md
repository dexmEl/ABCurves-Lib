# ABCurves Windows runtime

This package contains the Windows x64 `abcurves.dll`, all three planner models,
and the Renderer model. Install the Microsoft Visual C++ x64 runtime required by
your DLL build. No ABCurves model files are required at runtime.

* C: `API/c/examples/dll.c` loads the DLL using `LoadLibraryW`; include `API/c/include`.
* C++: `API/cpp/examples/dll.cpp` uses the header-only wrapper. Include
  `API/c/include` and `API/cpp/include`; no ABCurves import library is required.
* Python: install the separately built bundled wheel with `python -m pip install <wheel.whl>`.
  Alternatively install `API/python` with pip and set `ABCURVES_LIBRARY` to this DLL's full path.
* Rust: set `ABCURVES_LIBRARY` to the absolute DLL path and run
  `cargo run --manifest-path API/rust/Cargo.toml -p abcurves-native --example embedded`.

See the [language guides](../docs/getting-started/python.md) and [feature matrix](../docs/api/language-parity.md).

Place the DLL beside the executable for the C and C++ examples. The DLL is not
an operating-system-independent binary: Windows x64 and the appropriate MSVC
runtime are required. For source builds and full guides see the repository.
