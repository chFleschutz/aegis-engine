# Foundation plan: decisions and milestones

The work from [report.md](report.md) §7. The work items are GitHub issues, grouped into milestones
`M1`–`M8` (plus "After the foundation"); each issue holds the branch, size, the finding IDs it
closes, its "done when" check and its status, and its dependencies are "Blocked by" relations. This
file keeps what the issues don't: the decisions with their reasons, and each milestone's goal and
working order. Status lives only in the issues.

- **Closes** in an issue lists the canonical finding IDs, with merged duplicates in parentheses. The
  evidence (file:line, failure scenario, fix sketch) is in `findings-*.md`; verdicts and corrections
  are in `verify-*.md`, which win where the two disagree.
- **Done when** is the acceptance check. "Test n" refers to the first test batch in report §4.3.
  A test marked *fails today* is written first, as the bug report.
- **Size:** S = under 5 files or about a day, M = one module or a few days, L = crosses modules.
- Baseline: commit `5c4b20d9`. Line numbers in the findings drift as PRs land; re-read the cited
  code at the start of each PR. PR #91 (`import std`, CMake 4.5, Vulkan SDK 1.4.363) has since
  touched 167 files, mostly includes, so expect most line numbers to be off.

## How to work an item

The full workflow is in AGENTS.md "Commits and pull requests". In short:

1. Pick an open issue whose "Blocked by" issues are closed, or merged as far as stacking allows, and
   read it: `gh issue view <n>`.
2. `tools/new-worktree.sh <branch> [base]` with the branch name from the issue; for an item whose
   blocker is still unmerged, pass that branch as `base` (stacked PR).
3. Read only the IDs listed under **Closes**, then re-check them against the current code.
4. Write the *fails today* test first, where there is one.
5. Keep mechanical commits (renames, deletions, signature churn) separate from logic commits.
6. Each commit: builds clean, `ctest -LE device` passes. Before pushing: the full `ctest` including
   the `device` tests; for render changes, compare `aegis-test --headless --scene --screenshot` and
   `--gpu-driven --screenshot`.
7. `tools/format.sh`, then `gh pr create` with `Closes #<issue>` in the body; check CI; leave the
   merge to the owner.
8. After merge: `tools/remove-worktree.sh <branch> --delete-branch`; add mechanical commit hashes
   to `.git-blame-ignore-revs`.

Up to about three items can run in parallel, one worktree each. Don't run two items that edit the
same central file at once.

## Decisions

Taken 2026-10-17 (report §8 lists the original questions).

- **D1. Error types: one type per module, same shape.** Each module's `Error` is
  `{ ErrorCode code; std::string message; std::optional<rhi::Error> rhi; }` (rhi keeps
  `{code, debug location}`), built with `makeError(code, msg)` / `makeError(lowerError, msg)` and
  printed only through the module's `describe(const Error&)`. Wrapping adds context and copies the
  rhi cause; it never re-stringifies. Why: callers branch on module codes (`NotReady`, `OutOfDate`,
  `FormatNotSupported`); the problem is lost causes, not the number of types. → 3.2
  ([#59]–[#62]).
- **D2. Static/dynamic: hybrid.** Static entities stay frozen after `begin`. Structural changes
  (entity create/destroy, `Mesh`/`Material` add/remove) bump a static-set version automatically
  through the existing entt signals, and the static set is rebuilt when it changes. Moving a static
  entity is explicit: the editor calls `markStaticDirty(entity)` after gizmo/inspector edits;
  scripts that move their entity use `DynamicTag`. The rule is documented at `DynamicTag` and in
  AGENTS.md. Why: all writes go through component references, which entt can't observe; automatic
  tracking would need a setter API everywhere. Revisit with `InstanceStore` (7.1, [#86]). → 1.7
  ([#40]), 1.9 ([#44]).
- **D3. Logging: drop the `aegis-log` dependency.** Replace it with the simple `aegis::log`
  in the foundation module (stderr with a severity tag, no setup, one `setSink` for tests). It can
  come back later as an app-installed sink if file or async output is wanted. → 3.1 ([#58]).
- **D4. Foundation module: `aegis.base` now, renamed `aegis.core` once the legacy core is
  gone** (core is the better name). The profiler lives in it (public ctor, `steady_clock`, one
  documented default instance). → 3.1 ([#58]), 6.7 ([#85]).
- **D5. Sponza alpha-mask/double-sided materials: a feature of the material system,** not
  foundation work. 5.2 ([#73]) carries the alpha and sidedness data so the feature only adds the
  pipeline key and the masked variant. → [#135], "After the foundation".
- **D6. Preserve mirrored transforms.** `decomposeTRS` keeps negative scale; check winding of
  mirrored instances in the same PR. → 1.8c ([#43]); the front-face flip is still open (Polish).
- **D7. Game-usage model: the engine hosts the game** (2026-10-19, see
  [`docs/editor-runtime-split.md`](../editor-runtime-split.md)).
  - **Executables:** Aegis owns `main()`. `aegis_add_game` builds one engine-owned launcher into
    `<Game>` (runtime) and `<Game>-Editor`, and only the editor executable links `aegis-editor`.
  - **A game** is a C++ game module (`AEGIS_GAME_MODULE`, one registration function, statically
    linked) plus a project folder of data. A DLL/hot-reload build can come later through the same
    entry point.
  - **Data:** scenes are files, loaded through a type registry that engine and game register into
    alike. Entities and assets are referenced by stable ID.
  - **Scripting language:** planned for (scripts attached by name, state in registered fields, fixed
    hooks), not built.
  - Why: the editor stays out of shipped games, a game needs no `main()` of its own, and scenes
    become savable and editable. Save games need the same serializer anyway.
  - → [M6][m6] (6.1–6.12), export ([#134]). Decides F1.
- **D8. Asset system: sources in the project, imported data in a cache** (2026-10-19, see
  [`docs/asset-system.md`](../asset-system.md)).
  - **Identity:** `AssetId` is one random 64-bit ID (16 lowercase hex digits in files) for every
    asset and every part of a file, found through an index (scanned in development, a pak table when
    shipped). Foreign source files (glTF, OBJ, PNG, HDR) get a `<file>.<ext>.meta` sidecar with the
    ID, import settings and a `key → id` table for their parts; native files (scenes, materials) carry
    their ID in a common `{type, id, version}` header. Engine content is a read-only folder of normal
    assets, with no reserved IDs.
  - **Fixed formats (2026-10-20):** JSON through glaze; per-type versions with single-step migration
    chains; random local entity IDs, with instance entity IDs derived by XXH3-64; a flat entity list
    with `children` arrays; quaternions `[x, y, z, w]`, meters, radians, linear colors, right-handed
    Z-up converted at import; paths case-sensitive everywhere. Pinned by golden-file tests.
  - **Import cache:** in `<project>/.aegis/`, the only git-ignored folder, keyed by a hash of source,
    settings, importer version and platform. Its format is the cooked format, so cooking packs it.
  - **Runtime:** `AssetLibrary::load<T>(AssetId)` returns an `AssetRef<T>` (a shared slot with a load
    state), deduplicated by ID. Components store `AssetRef`s; the serializer writes only IDs.
  - **Scenes and worlds:** a `World` is the live object, a `Scene` the asset. Prefabs are scenes, not
    a type; `instantiate(world, scene, parent)` serves levels, prefabs and imported models. Scene
    files store local entity IDs, and world IDs derive from instance and local ID. "Level" is not an
    engine type. `scene::Scene` becomes `World` and `aegis.scene` becomes `aegis.world`.
  - **Asset types:** `Texture`, `StaticMesh`, `Material` (was `MaterialInstance`),
    `MaterialTemplate` (engine content), `Environment`, `Scene`. The `Mesh` and `Material` components merge
    into `MeshRenderer`, and the `Environment` component becomes `Sky`.
  - Why: source stays authoritative and reimports itself; imported data, which depends on importer
    version and shader-pinned layouts, is never committed, so a layout change needs no migration.
    Sidecars survive moves and git merges, unlike paths or one database file.
  - → 6.9 ([#130]), 6.10 ([#131]), 6.13–6.16 ([#137]–[#140]), export ([#134]); later material
    files ([#141]), prefab overrides ([#142]) and hot reload ([#143]). Decides F2.

Smaller decisions:
- **Subproject use:** make paths subproject-safe now (3.5, [#65]). How a game consumes the engine
  (subproject, package, install rules) is a future feature and decision (F1 below).
- **Keep the CPU geometry path** as the fallback for devices without mesh shaders (and the
  visual reference for the GPU-driven path). 7.1 ([#86]) makes both draw from the same data.
- **Formatting: the whole tree is formatted;** drop the diff-only rule from AGENTS.md (0.1).
  A CI check is optional (8.3, [#90]).
- **Reference images: local comparison** against images from the dev GPU (8.1, [#88]). lavapipe CI
  is not a priority ([#136], "After the foundation").
- **Commit trailers:** keep the AGENTS.md rule, no `Co-Authored-By` on any commit.

Future decisions (not blocking):
- **F1. How a game uses the engine (decided by D7):** engine-as-host, and `aegis.app` owns `main`.
  Asset and shader lookup at runtime is part of 6.11 ([#132]); install rules are part of the export
  step ([#134]). That step also settles how a game outside this repo consumes the engine: as a
  subproject (3.5 makes that safe) or as an installed package.
- **F2. Asset system design (decided by D8):** sidecars, import cache, `AssetRef`, scenes as prefabs.
  Still open, none blocking 6.9: the text format (with 6.10), the cooked blob layout, texture
  compression, `.blend` import, overrides, hot reload and streaming (`docs/asset-system.md`, "Open").

---

## Milestones

**M0. Docs** (direct to `main`, no issues) is done: the stale docs were fixed in 1c99492e and
`.git-blame-ignore-revs` started in 1c072de3.

**[M1. Independent bug fixes][m1]** (done) fixed the audit's bugs that needed no design change, in
parallel lanes: tests and math, render, content. Its one leftover, the front-face flip for mirrored
instances (D6), is under Polish.

**[M2. Mechanical cleanup][m2]** deletes dead code and renames, each in its own blame-ignored PR,
following the dead-code verdicts in `verify-conventions.md` §3. Only the renames ([#56]) remain;
they waited for M1, which is done.

**[M3. Foundation policy][m3]** (done) set the shared rules the later milestones build on:
`aegis.base` (D3, D4), the D1 error shape in every new-style module, the shader build helper with
`spirv-val`, the guarded CPU/GPU data contract, subproject-safe paths and link visibility.

**[M4. Render-graph authoring][m4]** makes passes declare their resources once and lets the graph
plan and own its textures. Order: 4.2 ([#68]) → 4.3 ([#69]); 4.4 ([#70]) runs in parallel with 4.2.
4.2 also unblocks 7.1 and the material system.

**[M5. Content and platform][m5]** splits CPU-side import from GPU upload and moves instantiation
out of `aegis.assets`, and gives input and windowing explicit APIs. It runs in parallel with M4. Two
chains: 5.1 ([#72]) → 5.2 ([#73]) → 5.3 ([#74]) → 5.4 ([#75]), and 5.5 ([#76]) → 5.6 ([#77]);
5.7 ([#78]) is independent. 5.6 and 5.7 gate M6 (6.4 and 6.1), 5.2 gates the asset IDs (6.9), and
5.4 gates the `aegis.core` rename (6.7).

**[M6. `aegis.app`][m6]** (P7) makes the engine host the game (D7, reworked 2026-10-19 after
[`docs/editor-runtime-split.md`](../editor-runtime-split.md)). One PR per step; examples port one
per commit. Working order, so the numbers are not sequential: 6.1 ([#79]) and 6.2 ([#80]), the type
registry 6.8 ([#129], done), then 6.3 ([#81]) and 6.4 ([#82]). The asset chain (D8): the `World`
rename 6.14 ([#138], right after 6.2) and the asset-type renames 6.15 ([#139], after 5.3), then asset
IDs 6.9 ([#130]), the import cache 6.13 ([#137]) and scene files 6.10 ([#131]), then scene instances
6.16 ([#140]). Then the game launcher 6.11 ([#132]); then the editor on top, 6.5
([#83]) and 6.12 ([#133]), and `aegis-test` on the app, 6.6 ([#84]). The rename to `aegis.core`,
6.7 ([#85]), comes last, once the legacy core is gone.

**[M7. Minimal SceneView, then split][m7]** (P8) gives passes a `FrameScene` and one
`InstanceStore` for both geometry paths, then splits the render graph into framework and passes.
Do it before the first terrain, vegetation or shadow pass. 7.1 ([#86]) waits for 4.2 and 6.3;
7.2 ([#87], mechanical) follows 7.1. The mirrored-instance fix (Polish) lands with 7.1 at the
latest.

**[M8. Verification tiers][m8]** (P9, done) added local reference-image comparison and the CI
format check. The missing-include scanner (8.2) was dropped: since PR #91, `import std` gives both
standard libraries the same module, so the failures it was meant to catch no longer happen.

**[After the foundation][later]** holds the features that build on it: the material system's
alpha-mask and double-sided materials ([#135], D5), lavapipe device tests in CI ([#136], not a
priority), exporting a game ([#134], D7/F1/D8, waits for 6.9, 6.11 and 6.13), and the later asset
features (D8): native material files ([#141]), prefab overrides and unpacking ([#142]), and asset
hot reload ([#143]).

---

## Polish: fix when touching the file

No PR of their own. Pick them up when a PR already edits the file.

- B-RENDER-11: skipped lighting leaves SceneColor undefined; process-wide `static reported`.
- B-BUILD-12: IBL bakes sample texel corners, not centers (all four IBL shaders).
- B-CONTENT-14: RGBA32F textures linear-filtered without a format-feature check.
- B-APP-15 (E-APP-08, B-FOUND-06): camera inspector accepts degenerate FOV/near/far; vacuous
  aspect assert.
- E-RENDER-10: `static_assert` invariants in `:vulkan_conversions`.
- A-CONTENT-04 dedupe of images shared by several glTF textures (with the asset cache).
- C-05: one native-handle accessor spelling (needs the formatting/naming decision).
- Mirrored instances render inside out on both paths (left over from 1.8c, D6): both pipelines
  cull back faces with a fixed front face. Needs the determinant sign in the instance data, plus a
  second pipeline on the CPU path and reversed winding or shader culling on the GPU path. After
  3.4 (PR #124): a flags word fits in `InstanceData`'s 4 padding bytes after `drawBatch`, set from
  `determinant(mat3(model)) < 0` in `detail::toGpuTransform`; the mesh shader swaps two indices per
  primitive; `StaticMeshConstants` has 16 spare bytes for the CPU path. Do it with 7.1 ([#86]) at
  the latest.

## Deferred until the feature

From report §3: persistent and imported graph images, pooled pipelines and `ShaderLibrary`,
multi-queue submit, asset cache and streaming, job system, transient aliasing. From D7: prefabs and
the entity hierarchy's file format, automatic field lists for the type registry, a DLL/hot-reload
game build, a scripting language.

[m1]: https://github.com/chFleschutz/aegis-engine/milestone/1?closed=1
[m2]: https://github.com/chFleschutz/aegis-engine/milestone/2
[m3]: https://github.com/chFleschutz/aegis-engine/milestone/3?closed=1
[m4]: https://github.com/chFleschutz/aegis-engine/milestone/4
[m5]: https://github.com/chFleschutz/aegis-engine/milestone/5
[m6]: https://github.com/chFleschutz/aegis-engine/milestone/6
[m7]: https://github.com/chFleschutz/aegis-engine/milestone/7
[m8]: https://github.com/chFleschutz/aegis-engine/milestone/8?closed=1
[later]: https://github.com/chFleschutz/aegis-engine/milestone/9
[#40]: https://github.com/chFleschutz/aegis-engine/issues/40
[#43]: https://github.com/chFleschutz/aegis-engine/issues/43
[#44]: https://github.com/chFleschutz/aegis-engine/issues/44
[#56]: https://github.com/chFleschutz/aegis-engine/issues/56
[#58]: https://github.com/chFleschutz/aegis-engine/issues/58
[#59]: https://github.com/chFleschutz/aegis-engine/issues/59
[#62]: https://github.com/chFleschutz/aegis-engine/issues/62
[#65]: https://github.com/chFleschutz/aegis-engine/issues/65
[#68]: https://github.com/chFleschutz/aegis-engine/issues/68
[#69]: https://github.com/chFleschutz/aegis-engine/issues/69
[#70]: https://github.com/chFleschutz/aegis-engine/issues/70
[#72]: https://github.com/chFleschutz/aegis-engine/issues/72
[#73]: https://github.com/chFleschutz/aegis-engine/issues/73
[#74]: https://github.com/chFleschutz/aegis-engine/issues/74
[#75]: https://github.com/chFleschutz/aegis-engine/issues/75
[#76]: https://github.com/chFleschutz/aegis-engine/issues/76
[#77]: https://github.com/chFleschutz/aegis-engine/issues/77
[#78]: https://github.com/chFleschutz/aegis-engine/issues/78
[#79]: https://github.com/chFleschutz/aegis-engine/issues/79
[#80]: https://github.com/chFleschutz/aegis-engine/issues/80
[#81]: https://github.com/chFleschutz/aegis-engine/issues/81
[#82]: https://github.com/chFleschutz/aegis-engine/issues/82
[#83]: https://github.com/chFleschutz/aegis-engine/issues/83
[#84]: https://github.com/chFleschutz/aegis-engine/issues/84
[#85]: https://github.com/chFleschutz/aegis-engine/issues/85
[#86]: https://github.com/chFleschutz/aegis-engine/issues/86
[#87]: https://github.com/chFleschutz/aegis-engine/issues/87
[#88]: https://github.com/chFleschutz/aegis-engine/issues/88
[#90]: https://github.com/chFleschutz/aegis-engine/issues/90
[#129]: https://github.com/chFleschutz/aegis-engine/issues/129
[#130]: https://github.com/chFleschutz/aegis-engine/issues/130
[#131]: https://github.com/chFleschutz/aegis-engine/issues/131
[#132]: https://github.com/chFleschutz/aegis-engine/issues/132
[#133]: https://github.com/chFleschutz/aegis-engine/issues/133
[#134]: https://github.com/chFleschutz/aegis-engine/issues/134
[#135]: https://github.com/chFleschutz/aegis-engine/issues/135
[#136]: https://github.com/chFleschutz/aegis-engine/issues/136
[#137]: https://github.com/chFleschutz/aegis-engine/issues/137
[#138]: https://github.com/chFleschutz/aegis-engine/issues/138
[#139]: https://github.com/chFleschutz/aegis-engine/issues/139
[#140]: https://github.com/chFleschutz/aegis-engine/issues/140
[#141]: https://github.com/chFleschutz/aegis-engine/issues/141
[#142]: https://github.com/chFleschutz/aegis-engine/issues/142
[#143]: https://github.com/chFleschutz/aegis-engine/issues/143
