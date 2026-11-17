# Static/dynamic: hybrid

Formerly D2. Decided 2026-10-17.

Static entities stay frozen after `begin`. Structural changes
(entity create/destroy, `Mesh`/`Material` add/remove) bump a static-set version automatically
through the existing entt signals, and the static set is rebuilt when it changes. Moving a static
entity is explicit: the editor calls `markStaticDirty(entity)` after gizmo/inspector edits;
scripts that move their entity use `DynamicTag`. The rule is documented at `DynamicTag` and in
AGENTS.md. Why: all writes go through component references, which entt can't observe; automatic
tracking would need a setter API everywhere. Revisit with `InstanceStore` (7.1, [#86](https://github.com/chFleschutz/aegis-engine/issues/86)). → 1.7
([#40](https://github.com/chFleschutz/aegis-engine/issues/40)), 1.9 ([#44](https://github.com/chFleschutz/aegis-engine/issues/44)).
