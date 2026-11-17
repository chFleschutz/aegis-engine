# Architecture decisions

One file per decision, `NNNN-slug.md`. Cite one by its file name, never by a bare number. The first
eight were D1–D8 in the audit's decision list and keep their numbers here, so old citations in closed
issues and commits still resolve through the last column.

| ADR | Decision | Status | Formerly |
| --- | --- | --- | --- |
| [0001](0001-one-error-type-per-module.md) | Error types: one type per module, same shape | Accepted | D1 |
| [0002](0002-static-dynamic-hybrid.md) | Static/dynamic: hybrid | Accepted | D2 |
| [0003](0003-drop-aegis-log.md) | Logging: drop the `aegis-log` dependency | Accepted | D3 |
| [0004](0004-foundation-module-name.md) | Foundation module: `aegis.base` now, renamed `aegis.core` once the legacy core is gone | Accepted | D4 |
| [0005](0005-alpha-mask-in-material-system.md) | Sponza alpha-mask/double-sided materials: a feature of the material system, not foundation work | Accepted | D5 |
| [0006](0006-preserve-mirrored-transforms.md) | Preserve mirrored transforms | Accepted | D6 |
| [0007](0007-engine-hosts-the-game.md) | Game-usage model: the engine hosts the game | Accepted | D7 |
| [0008](0008-asset-sources-and-import-cache.md) | Asset system: sources in the project, imported data in a cache | Accepted | D8 |
| [0009](0009-keep-cpu-geometry-path.md) | Keep the CPU geometry path | Accepted | |
| [0010](0010-local-reference-images.md) | Reference images: local comparison | Accepted | |
| [0011](0011-subproject-safe-paths.md) | Subproject use | Accepted | |
| [0012](0012-views-as-scoped-subgraphs.md) | Views render as scoped subgraphs of one frame graph, culling per view | Accepted | |
