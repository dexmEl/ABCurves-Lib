# ABCurves Rust binding

Start with the safe `abcurves-native` crate: [installation and complete example](../../docs/getting-started/rust.md).

[Integration](../../docs/integration.md) | [Examples](../../examples/README.md) | [Advanced reference](../../docs/advanced/README.md)

The workspace keeps `abcurves` and `abcurves-sys` as sibling crates. Windows loads the DLL dynamically by default; configure its path as described in the Rust guide. Import-library linking remains an explicit option. No crates.io release is claimed.
