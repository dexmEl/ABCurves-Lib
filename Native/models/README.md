# Reference model assets

`renderer_global_h80.bin` is the optional original Renderer runtime image. `continuous/`, `continuous-16/`, and `continuous-8/` preserve learned source/export representations for the original Python reference and model exporter. Static Planner checkpoints and float/adapter Renderer assets support upstream regression tests; they are not required by Planner Only production use.

The native planner loads only `lib/models/{32,16,8}ms/{manifest.json,model.abcmodel}`. Do not substitute a `.pt`, `.onnx` or `.npz` for that native bundle. No export occurs in normal builds.

Assets are retained byte-for-byte and covered by MIT as confirmed by the maintainer. Preserve the upstream license and copyright notices. [Asset identities](../../docs/evidence/model-assets.json), [licensing and attribution](../../docs/licensing.md), [upstream model documentation](https://github.com/optima-manent/ABCurves/blob/main/models/README.md).
