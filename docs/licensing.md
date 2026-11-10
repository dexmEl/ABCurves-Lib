# Licensing and attribution

The software, model weights, exported model representations and embedded Renderer tables are covered by the upstream [MIT license](../LICENSE). The project maintainer confirmed that coverage on 2026-11-10. Dataset material and derived examples retain their separate [CC BY 4.0 terms](../Native/DATASET_LICENSE.md).

## MIT-covered components

MIT permits use, modification and redistribution, including commercial applications. Include the upstream copyright notice and complete MIT license text with copies or substantial portions. A credit line alone does not replace those notices. MIT does not require publishing your application's source code.

Preserve Optima Manent's notice and [upstream attribution](../NOTICE). This fork modifies the implementation; no upstream endorsement is implied. Keep LICENSE and NOTICE with native binaries, source distributions and separately distributed model bundles. Python wheels carry the same notices.

Model images remain byte-identical to the validated assets. [The asset inventory](evidence/model-assets.json) records their identities and roles. Licensing confirmation changes publication policy; it does not change weights, inference, Renderer math or ABI 1.4.

## Dataset material

Raw Capture collections and prepared numerical derivatives use CC BY 4.0. Credit ABCurves and Optima Manent, link to the upstream project and license, and identify preparation/compression or other changes. NOTICE contains the attribution used for reference fixtures and derived examples. Do not replace these data terms with the software's MIT label.

Large raw/prepared datasets and maintainer research archives are excluded from publication by scope, while useful source and curated regression resources remain public.

## Provenance and confirmation

The former nested Native repository recorded upstream base commit `e197441f8d343d3b4a4069c7cae20764d0953d15`. The modified source is included directly as ordinary files; no submodule initialization is required. Original nested Git metadata remains in the ignored maintainer archive. This revision identifies the upstream base, not an assertion that this fork is byte-identical to it.

The upstream LICENSE, README, model README and DATASET_LICENSE were checked on 2026-11-10. Code and dataset terms were explicit in those documents. The model/embedded-table coverage recorded here is based on the maintainer's subsequent confirmation; no new upstream response or independent legal review is represented. That confirmation supersedes the earlier model-licensing hold retained in historical local reports.

## Package scope

[Publication policy](../packaging/publication.json) has no licensing restrictions on these model assets. The source package includes runtime and retained reference models. The native SDK includes selected model bundles (16 ms by default); Python wheels keep external models separate. This is a package-size choice. Include LICENSE and NOTICE when distributing selected bundles separately.

[Packaging instructions](../packaging/README.md) describe the source/SDK layouts. Staging never uploads or publishes anything. The legacy `--local-validation-assets` option remains accepted and has no effect while the restricted-assets list is empty.

Before release, choose the actual hosting/deployment policy and review the Git index/history for unintended content. Only Windows x64 has executed qualification; Linux/macOS remain unverified portability targets.
