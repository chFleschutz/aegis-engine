# Foundation plan: milestones

The work from [report.md](audit/report.md) §7. The work items are GitHub issues, grouped into milestones
(`M1`–`M14`, then "Backlog"; the numbers are IDs in creation order, not the working order), with
their dependencies as "Blocked by" relations; labels give the kind,
size, area and the `needs-gpu` / `needs-design` flags (AGENTS.md "Planned work"). This file keeps
what the issues don't: each milestone's goal and working
order. Status lives only in the issues: an assigned issue is being worked on.

- **Closes** in an issue lists the canonical finding IDs, with merged duplicates in parentheses. The
  evidence (file:line, failure scenario, fix sketch) is in `findings-*.md`; verdicts and corrections
  are in `verify-*.md`, which win where the two disagree.
- **Done when** is the acceptance check. "Test n" refers to the first test batch in report §4.3.
  A test marked *fails today* is written first, as the bug report.
- **Size:** S = under 5 files or about a day, M = one module or a few days, L = crosses modules or
  more than about 1500 lines / 30 files (PRs #151 and #164 were rated M and came out larger).
- Baseline: commit `5c4b20d9`. Line numbers in the findings drift as PRs land; re-read the cited
  code at the start of each PR. PR #91 (`import std`, CMake 4.5, Vulkan SDK 1.4.363) has since
  touched 167 files, mostly includes, so expect most line numbers to be off.

How to work an item: AGENTS.md "Commits and pull requests" and the `/implement` skill. Up to
about three items run in parallel across the two lanes (see **Lanes** below), one worktree each,
never two with the same `area/*` label.

## Decisions

The decisions are architecture decision records in [`docs/adr/`](adr/README.md).

---

## Milestones

**M0. Docs** (direct to `main`, no issues) is done: the stale docs were fixed in 1c99492e and
`.git-blame-ignore-revs` started in 1c072de3.

**[M1. Independent bug fixes](https://github.com/chFleschutz/aegis-engine/milestone/1?closed=1)** (done) fixed the audit's bugs that needed no design change, in
parallel lanes: tests and math, render, content. Its one leftover, the front-face flip for mirrored
instances ([`0006-preserve-mirrored-transforms.md`](adr/0006-preserve-mirrored-transforms.md)), is under Polish.

**[M2. Mechanical cleanup](https://github.com/chFleschutz/aegis-engine/milestone/2?closed=1)** (done) deleted dead code and renamed, each in its own
blame-ignored PR, following the dead-code verdicts in `verify-conventions.md` §3.

**[M3. Foundation policy](https://github.com/chFleschutz/aegis-engine/milestone/3?closed=1)** (done) set the shared rules the later milestones build on:
`aegis.base` ([`0003-drop-aegis-log.md`](adr/0003-drop-aegis-log.md), [`0004-foundation-module-name.md`](adr/0004-foundation-module-name.md)), the error shape of [`0001-one-error-type-per-module.md`](adr/0001-one-error-type-per-module.md) in every new-style module, the shader build helper with
`spirv-val`, the guarded CPU/GPU data contract, subproject-safe paths and link visibility.

**[M4. Render-graph authoring](https://github.com/chFleschutz/aegis-engine/milestone/4?closed=1)** (done) made passes declare their resources once and let the
graph plan and own its textures.

**[M5. Content and platform](https://github.com/chFleschutz/aegis-engine/milestone/5?closed=1)** (done) split CPU-side import from GPU upload, moved
instantiation out of `aegis.assets`, and gave input and windowing explicit APIs.

**M6, first part** (done, under the old name "`aegis.app`"): `Engine::create` and headless mode
([#79](https://github.com/chFleschutz/aegis-engine/issues/79)), no engine singleton ([#80](https://github.com/chFleschutz/aegis-engine/issues/80)), the type registry ([#129](https://github.com/chFleschutz/aegis-engine/issues/129)), the world lifecycle ([#81](https://github.com/chFleschutz/aegis-engine/issues/81)),
frame phases ([#82](https://github.com/chFleschutz/aegis-engine/issues/82)), the `World` rename ([#138](https://github.com/chFleschutz/aegis-engine/issues/138)) and the asset types ([#139](https://github.com/chFleschutz/aegis-engine/issues/139)). On 2026-10-21 the
rest was split by goal: the asset work ([`0008-asset-sources-and-import-cache.md`](adr/0008-asset-sources-and-import-cache.md)) stayed in M6, the launcher and editor ([`0007-engine-hosts-the-game.md`](adr/0007-engine-hosts-the-game.md)) moved to M9.
Item numbers like "6.9" are gone from the titles; refer to items by issue number.

**[M6. Assets and scene files](https://github.com/chFleschutz/aegis-engine/milestone/6)** ([`0008-asset-sources-and-import-cache.md`](adr/0008-asset-sources-and-import-cache.md)): asset IDs ([#130](https://github.com/chFleschutz/aegis-engine/issues/130)) first. Then, in parallel, the
import cache for meshes ([#137](https://github.com/chFleschutz/aegis-engine/issues/137), `needs-design`) → KTX2 textures ([#169](https://github.com/chFleschutz/aegis-engine/issues/169)), and scene files
([#131](https://github.com/chFleschutz/aegis-engine/issues/131)). Before #169: derived sub-asset IDs ([#236](https://github.com/chFleschutz/aegis-engine/issues/236)) → settings per part ([#244](https://github.com/chFleschutz/aegis-engine/issues/244)), and the cache by
key ([#238](https://github.com/chFleschutz/aegis-engine/issues/238)). Scene instances follow both #130 and #131, in two steps: nested instances with derived
entity IDs ([#183](https://github.com/chFleschutz/aegis-engine/issues/183)), then imported models as scene assets ([#140](https://github.com/chFleschutz/aegis-engine/issues/140)).

**[M7. FrameScene and render-graph split](https://github.com/chFleschutz/aegis-engine/milestone/7)** (P8) gives passes a `FrameScene` and one
`InstanceStore` for both geometry paths, then splits the render graph into framework and passes.
Do it before the first terrain, vegetation or shadow pass. The `FrameScene` ([#181](https://github.com/chFleschutz/aegis-engine/issues/181)) is unblocked
and runs in parallel with M6; the `InstanceStore` ([#86](https://github.com/chFleschutz/aegis-engine/issues/86)) follows it, then [#87](https://github.com/chFleschutz/aegis-engine/issues/87) (mechanical). The
mirrored-instance fix ([#171](https://github.com/chFleschutz/aegis-engine/issues/171)) has landed.

**[M9. Game launcher and editor split](https://github.com/chFleschutz/aegis-engine/milestone/10)** ([`0007-engine-hosts-the-game.md`](adr/0007-engine-hosts-the-game.md)) makes the engine host the game
([`docs/editor-runtime-split.md`](editor-runtime-split.md)). The launcher follows scene files,
in two steps: the engine stops linking the editor ([#182](https://github.com/chFleschutz/aegis-engine/issues/182)), then the launcher itself ([#132](https://github.com/chFleschutz/aegis-engine/issues/132)). The
examples become one game ([#167](https://github.com/chFleschutz/aegis-engine/issues/167)) after it and scene instances, one example per commit.
The editor becomes a library ([#83](https://github.com/chFleschutz/aegis-engine/issues/83)); building the editor itself is M10. `aegis-test`: the per-check split ([#170](https://github.com/chFleschutz/aegis-engine/issues/170)) can start any time;
running its scenes through the app ([#84](https://github.com/chFleschutz/aegis-engine/issues/84)) follows [#132](https://github.com/chFleschutz/aegis-engine/issues/132). Last, the legacy core goes ([#172](https://github.com/chFleschutz/aegis-engine/issues/172), after
[#83](https://github.com/chFleschutz/aegis-engine/issues/83)) and `aegis.base` becomes `aegis.core` ([#85](https://github.com/chFleschutz/aegis-engine/issues/85)).

**[M10. Editor](https://github.com/chFleschutz/aegis-engine/milestone/11)** builds the editor on the runtime ([`docs/editor.md`](editor.md)). It
starts once [#83](https://github.com/chFleschutz/aegis-engine/issues/83) (M9) has made the editor a library.
- **Editor steps,** one at a time, since all are `area/editor`: the viewport ([#200](https://github.com/chFleschutz/aegis-engine/issues/200)), the
  registry-driven inspector ([#133](https://github.com/chFleschutz/aegis-engine/issues/133)), undo of field edits ([#165](https://github.com/chFleschutz/aegis-engine/issues/165)) and of structural edits ([#201](https://github.com/chFleschutz/aegis-engine/issues/201)),
  play-in-editor with saving ([#166](https://github.com/chFleschutz/aegis-engine/issues/166)), the asset browser ([#202](https://github.com/chFleschutz/aegis-engine/issues/202)) and the console ([#203](https://github.com/chFleschutz/aegis-engine/issues/203)). A headless
  smoke test ([#205](https://github.com/chFleschutz/aegis-engine/issues/205)) comes first and grows with them.
- **Runtime prerequisites,** in other lanes, ahead of the steps that need them:
  - type metadata and change hooks ([#191](https://github.com/chFleschutz/aegis-engine/issues/191));
  - render views ([#192](https://github.com/chFleschutz/aegis-engine/issues/192)) and UI textures ([#193](https://github.com/chFleschutz/aegis-engine/issues/193));
  - several worlds ([#194](https://github.com/chFleschutz/aegis-engine/issues/194)) and subtree snapshots ([#195](https://github.com/chFleschutz/aegis-engine/issues/195));
  - asset types in the database ([#196](https://github.com/chFleschutz/aegis-engine/issues/196));
  - child order ([#197](https://github.com/chFleschutz/aegis-engine/issues/197));
  - window scale and close veto ([#198](https://github.com/chFleschutz/aegis-engine/issues/198));
  - log sinks ([#199](https://github.com/chFleschutz/aegis-engine/issues/199)).
- **Last,** the UI design is tested and refined ([#204](https://github.com/chFleschutz/aegis-engine/issues/204), `needs-design`).

**[M11. Render foundation](https://github.com/chFleschutz/aegis-engine/milestone/13)** prepares the renderer for new materials, views and lights, so
that each of M12's features stays one PR. It runs in parallel with M10.
- **Render lane,** one at a time, since all are `area/render_graph`: graph resources that persist
  across frames ([#253](https://github.com/chFleschutz/aegis-engine/issues/253)), then several views per frame ([#254](https://github.com/chFleschutz/aegis-engine/issues/254), `needs-design`); lights in a GPU
  buffer, with spot lights ([#255](https://github.com/chFleschutz/aegis-engine/issues/255)); post-processing settings as scene data ([#256](https://github.com/chFleschutz/aegis-engine/issues/256), after #254, so
  the settings go on the views).
- **Pipelines per technique and material variant** ([#251](https://github.com/chFleschutz/aegis-engine/issues/251)) are `area/assets` too: they go next in
  the render lane once the assets lane is free (M6 and [#217](https://github.com/chFleschutz/aegis-engine/issues/217) merged), since #135 and the shadows need
  them. Material templates with their own shader ([#252](https://github.com/chFleschutz/aegis-engine/issues/252), `needs-design`) follow #251.
- **Build speed:** the CPU-side asset code moves off `aegis.rhi` ([#217](https://github.com/chFleschutz/aegis-engine/issues/217)) in the assets lane, after
  M6's open items.

**[M12. Render features](https://github.com/chFleschutz/aegis-engine/milestone/14)** adds what a game shows on screen, on both geometry paths:
- alpha-mask and double-sided materials ([#135](https://github.com/chFleschutz/aegis-engine/issues/135), [`0005-alpha-mask-in-material-system.md`](adr/0005-alpha-mask-in-material-system.md), after #251);
- shadows, cascaded for the directional light first, then spot and point lights (after #251 and
  #254; issue to be filed);
- blended materials in a forward pass (issue to be filed), then back-to-front sorting ([#9](https://github.com/chFleschutz/aegis-engine/issues/9));
- vertex tangents ([#19](https://github.com/chFleschutz/aegis-engine/issues/19)) at any point; they change the vertex layout and the reference images;
- anti-aliasing and auto exposure, after #253 (issues to be filed);
- last, clustered light culling ([#22](https://github.com/chFleschutz/aegis-engine/issues/22), `needs-design`, after #255), once scenes have hundreds of
  lights.

**[M13. Gameplay foundation](https://github.com/chFleschutz/aegis-engine/milestone/15)** gives a game a simulation loop and the services gameplay needs.
It starts once play-in-editor ([#166](https://github.com/chFleschutz/aegis-engine/issues/166)) is merged, so gameplay can be tried in the editor:
- the fixed-step phase and the hierarchy updated in order ([#257](https://github.com/chFleschutz/aegis-engine/issues/257), after [#219](https://github.com/chFleschutz/aegis-engine/issues/219) and [#194](https://github.com/chFleschutz/aegis-engine/issues/194)), then
  scripts get time, spawning, lookup and events ([#258](https://github.com/chFleschutz/aegis-engine/issues/258));
- then physics, audio and input actions (issues to be filed). The libraries are the owner's call;
  Jolt Physics and miniaudio are the recommendation, with their headers only in `.cpp` files;
- save games ([#168](https://github.com/chFleschutz/aegis-engine/issues/168)) can start at any point; they are `area/world` too.

**[M14. Vertical slice and shipping](https://github.com/chFleschutz/aegis-engine/milestone/16)** builds a small game in the sample project on M12 and
M13 (issue to be filed), and with it what a shipped game needs. The game is the first pass/fail
target beyond the render tests: what it lacks becomes issues here.
- background work: a job system ([#259](https://github.com/chFleschutz/aegis-engine/issues/259)), then background asset loading ([#260](https://github.com/chFleschutz/aegis-engine/issues/260), `needs-design`,
  after [#154](https://github.com/chFleschutz/aegis-engine/issues/154));
- the later asset features ([`0008-asset-sources-and-import-cache.md`](adr/0008-asset-sources-and-import-cache.md)): hot reload ([#143](https://github.com/chFleschutz/aegis-engine/issues/143)), prefab overrides and unpacking ([#142](https://github.com/chFleschutz/aegis-engine/issues/142)) and
  native material files ([#141](https://github.com/chFleschutz/aegis-engine/issues/141));
- last, exporting the game ([#134](https://github.com/chFleschutz/aegis-engine/issues/134), [`0007-engine-hosts-the-game.md`](adr/0007-engine-hosts-the-game.md), [`0008-asset-sources-and-import-cache.md`](adr/0008-asset-sources-and-import-cache.md), `needs-design`): the cook step, install rules and the
  shipping build, tried on the slice.

**Lanes.** Milestones run in two lanes, each with its own Active and Planning milestone; the lanes'
areas and rules are in the [`plan-milestone` skill](../.claude/skills/plan-milestone/SKILL.md#milestone-rules).
- **render** (`render_graph`, `render_passes`, `renderer`, `rhi`): M16 Render foundation (Active), M17
  Shadows and materials (Planning), then the rest of Render features above: anti-aliasing, auto
  exposure, vertex tangents ([#19](https://github.com/chFleschutz/aegis-engine/issues/19)) and clustered light culling ([#22](https://github.com/chFleschutz/aegis-engine/issues/22)).
- **content** (`assets`, `world`, `app`, `editor`, `platform`): no milestone yet; next is Gameplay
  foundation above. Until it is planned, this lane works its areas' `Backlog` issues.
- The vertical slice needs both lanes' targets; plan it once they are done.

**[M8. Verification tiers](https://github.com/chFleschutz/aegis-engine/milestone/8?closed=1)** (P9, done) added local reference-image comparison and the CI
format check. The missing-include scanner (8.2) was dropped: since PR #91, `import std` gives both
standard libraries the same module, so the failures it was meant to catch no longer happen.

**[Backlog](https://github.com/chFleschutz/aegis-engine/milestone/9)** (was "After the foundation") holds unscheduled work, in no order: the legacy
modules' function bodies moved into `.cpp` files ([#219](https://github.com/chFleschutz/aegis-engine/issues/219), before M13's [#257](https://github.com/chFleschutz/aegis-engine/issues/257)), the Vulkan-Hpp
module built without debug info ([#220](https://github.com/chFleschutz/aegis-engine/issues/220)), reference images for the games and the editor ([#225](https://github.com/chFleschutz/aegis-engine/issues/225)),
a visible Template scene ([#224](https://github.com/chFleschutz/aegis-engine/issues/224)), the README usage section ([#2](https://github.com/chFleschutz/aegis-engine/issues/2)), building one target while
iterating ([#218](https://github.com/chFleschutz/aegis-engine/issues/218)), and the device-lost bug ([#154](https://github.com/chFleschutz/aegis-engine/issues/154), before M14's [#260](https://github.com/chFleschutz/aegis-engine/issues/260): its repro freezes the
owner's PC, never run it without asking).

**[Ideas](https://github.com/chFleschutz/aegis-engine/milestone/12)** (label `idea`) holds nice-to-have work nobody has committed to, in no order:
extracting model parts to their own files ([#245](https://github.com/chFleschutz/aegis-engine/issues/245)). Once one is wanted, move it to Backlog or a
milestone and drop the label.

---

## Polish: fix when touching the file

No PR of their own. Pick them up when a PR already edits the file.

- B-RENDER-11: skipped lighting leaves SceneColor undefined; process-wide `static reported`.
- B-BUILD-12: IBL bakes sample texel corners, not centers (all four IBL shaders).
- B-CONTENT-14: RGBA32F textures linear-filtered without a format-feature check.
- B-APP-15 (E-APP-08, B-FOUND-06): camera inspector accepts degenerate FOV/near/far; vacuous
  aspect assert.
- E-RENDER-10: `static_assert` invariants in `:vulkan_conversions`.
- C-05: one native-handle accessor spelling (needs the formatting/naming decision).

A-CONTENT-04 (images shared by several glTF textures) is closed by [#130](https://github.com/chFleschutz/aegis-engine/issues/130); the mirrored-instance
winding fix has its own issue ([#171](https://github.com/chFleschutz/aegis-engine/issues/171)).

## Deferred until the feature

From report §3: persistent and imported graph images, pooled pipelines and `ShaderLibrary`,
multi-queue submit, asset streaming, job system, transient aliasing. From [`0007-engine-hosts-the-game.md`](adr/0007-engine-hosts-the-game.md): automatic field lists
for the type registry, a DLL/hot-reload game build, a scripting language. (Prefabs, the hierarchy's
file format and the asset cache are now planned: [#131](https://github.com/chFleschutz/aegis-engine/issues/131), [#137](https://github.com/chFleschutz/aegis-engine/issues/137), [#140](https://github.com/chFleschutz/aegis-engine/issues/140), [#142](https://github.com/chFleschutz/aegis-engine/issues/142). So are persistent
and imported graph images, pooled pipelines and the job system: [#253](https://github.com/chFleschutz/aegis-engine/issues/253), [#251](https://github.com/chFleschutz/aegis-engine/issues/251), [#259](https://github.com/chFleschutz/aegis-engine/issues/259).)
