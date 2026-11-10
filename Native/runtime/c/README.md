# Original C99 Renderer runtime

This is the preserved upstream Renderer implementation, built into the native SDK through CMake. Production users normally consume the SDK's optional Renderer adapter; see [Renderer guide](../../../docs/advanced/renderer.md).

Standalone validation from repository root:

```powershell
cmake -S Native/runtime/c -B build/renderer
cmake --build build/renderer --config Release
ctest --test-dir build/renderer -C Release --output-on-failure
```

`ABC_RENDERER_BLOB` can select the test model. The default is `Native/models/renderer_global_h80.bin`. Tests can be disabled with `ABC_BUILD_TESTS=OFF`. Software attribution: [MIT](../../LICENSE). Model redistribution review: [licensing](../../../docs/licensing.md).
