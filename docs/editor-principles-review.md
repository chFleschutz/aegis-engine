# Editor UX plan: the design principles against the editor

A review of the editor as of `b2d2055a` against [`editor-ui-design-principles.md`](editor-ui-design-principles.md),
and the plan that follows from it. Each point of the principles is checked against the code, judged for whether it
applies to this engine, and either closed, planned as an item, or deferred with the trigger that brings it back. Plan
items are numbered **UX-n** for issues to cite. The earlier review ([`editor-ui-review.md`](editor-ui-review.md),
UI-01 to UI-33) is fully merged ([#272] to [#283]).

**How it was reviewed:** by reading `src/aegis/editor/`, the runtime it calls (`engine.cpp`, `reflect`, the asset
library) and the sample scenes. Contrast ratios are WCAG 2 ratios computed from the tokens in `ui/theme.h` and the
style in `ui/theme.cpp`. Nothing was run, so the costs named under UX-5 are to be measured, not quoted.

## Summary

The editor already has what the principles call the expensive-to-retrofit foundations: M10 built steps 1 to 5 of
their build order (editor model and undo, the reflection-driven inspector, the docked shell, the viewport, the
hierarchy and asset browser) and most of step 9 (theme, icons, DPI). What is left, by impact here:

1. **Crash safety is missing** (UX-3). There is no autosave, a save rewrites the scene file in place, and Play runs
   the game's C++ in the editor's process, so a script crash or a device loss takes the unsaved edits with it.
2. **One selected entity at a time** (UX-1, UX-4). `Selection` holds a set, but every click replaces it and every
   action uses the active entity. Multi-selection needs grouped undo entries first, the last undo feature that gets
   harder to add the longer it waits.
3. **UI cost grows with the world** (UX-5). No list is clipped: playing `stress/cubes` spawns 1,000,000 root
   entities, and the hierarchy rebuilds and draws a row for each of them every frame.
4. **Responsiveness of loading** waits for the job system and background loading ([#259], [#260], M14). The
   editor's part is a task UI after them (UX-11).
5. **Discoverability and feedback**: a command palette (UX-6), context menus where they are missing (UX-7), a
   visible selection in the viewport (UX-8), log lines that lead to their asset or entity (UX-9), and two contrast
   failures in the theme (UX-10).

Deferred, each with its trigger in the tables below: layout presets, custom shortcuts, multi-viewport, inspector
search, and Dear ImGui Test Engine. Not applicable: "build state" (game code and shaders build outside the editor).

**A correction to the principles:** play-in-editor is not open here. [#166] decided and built the world copy: Play
captures the edit world in memory (`copyWorld`), runs the copy as the play world and discards it on Stop. The
paragraph under "Decouple the editor from the renderer" should say so. Its cost is the one in point 1.

## Evaluation

### Layout principles

| Principle | State here | Verdict and action |
|---|---|---|
| Dockable panels, one strong default, save and restore | Done: `DockBuilder` default, per-project `.aegis/editor/imgui.ini`, View → Reset Layout | Nothing to do |
| Task presets and named layouts | Missing | Defer. Five user panels and three debug panels, and no second workspace to preset for. Revisit with the first one (material editing [#141], prefab editing [#142]); the layout is one ini blob, so it stays cheap to add |
| Conventional arrangement | Done, as the table asks: hierarchy left, inspector right, assets and console tabbed at the bottom, menu bar with play controls and the scene name, tools in the viewport toolbar | Log entries don't link back to objects: UX-9 |
| Restraint: dark neutral chrome, one accent | Done (`theme.h` tokens, accent `#4A8CD9`) | Two contrast failures: UX-10 |
| Density, UI scale | Done: 15 px font plus 2 × 4 px frame padding gives 23 px rows; UI-scale steps times the monitor's scale | Nothing to do |
| Context over modality | The inspector follows the selection, the toolbar shows the tool. Three modals: Open Scene, Save As, Unsaved Changes | Unsaved Changes is the one modal that should stay (closing is not undoable). Open Scene becomes a mode of the palette (UX-6). Save As is rare and one field: keep |

### UX priorities

| # | Principle | State here | Verdict and action |
|---|---|---|---|
| 1 | Responsiveness | Missing. `AssetLibrary` loads synchronously, so opening Sponza, dropping a model and Rescan freeze the editor, with no progress. Shaders compile at build time (slangc), so that part doesn't apply. Per-frame UI cost grows with the world (see UX-5) | Background loading ([#259], [#260]) is L, `needs-design` and waits for [#154]; too big to pull forward for the editor alone. Now: UX-5. After #260: UX-11 |
| 2 | Undo for everything | Done for every scene edit: fields, whole components, add/remove, attach/detach, rename, reparent and reorder, create, destroy, duplicate, instantiate. Drags coalesce, the saved marker is the dirty flag, Edit → Undo names the entry. Missing: one entry for several edits; the gizmo writes `Transform` outside `setField` | UX-1, UX-2. Selection and preferences stay out of the history, as in most editors. Asset edits ([#141]) will add their own edit kinds |
| 3 | Direct manipulation | Gizmo (move, rotate, scale, local/world, snap), drag to reorder and reparent, assets onto the viewport, the hierarchy and asset slots, drag-scrubbing on every number. Missing: multi-selection; exact picking (a ray against bounding spheres, so a large mesh's sphere takes clicks meant for what is inside it); entity references (no `FieldKind` for one, so a row has nowhere to be dropped) | UX-4 first. UX-17 for exact picking, UX-13 for entity references |
| 4 | Navigation and discoverability | One `ActionRegistry` feeds menus, tooltips and shortcuts; search in the hierarchy, assets, Add menu and asset picker; context menus on hierarchy rows and component headers. Missing: a palette; context menus on asset rows, the viewport and console lines; custom shortcuts; inspector search | UX-6, UX-7. Custom shortcuts: defer (one user, and chords are already data in the registry, so a bindings file later costs little). Inspector search: defer with UX-14, components are short |
| 5 | Immediate feedback | Selected rows, kind icons, live drags, `*` in the title and the menu bar, play tint, frame and label, warning and error counts with icons. Missing: a selected mesh shows only the gizmo, and nothing in Select mode (Q); nothing shows hover; Ctrl-inverted snapping is invisible; log lines are plain text | UX-8 now, UX-17 later; UX-9 |
| 6 | Progressive disclosure | Collapsible sections, ⋮ menus for Reset, Copy, Paste and Remove, the ID in a tooltip. `FieldMeta` has no group or "advanced" flag. No multi-edit | UX-14 with the first large component; UX-12 after UX-4 |
| 7 | Crash safety | Missing (see the summary) | UX-3, first: cheap, and in-process play raises the stakes |
| 8 | Reflection-driven inspectors | Done ([#133]): `property_model` maps `FieldKind` plus `FieldMeta` (widget hint, limits, step, read-only, tooltip) and `TypeMeta` (name, category, required) to rows; the Add menu comes from the registry. Hand-written still: the Renderer panel's `RenderPass::drawUI()` and Preferences | [#256] makes post-processing settings scene data, which the inspector then draws; retire `drawUI` there. Custom drawers: none needed yet. Shared widgets: UX-15 |

### Designs to avoid

| Avoid | State here | Action |
|---|---|---|
| Modal dialogs for routine work | Three modals, see "Context over modality" | UX-6 |
| Deep menus as the only path | Two levels at most (Create → category); actions also have shortcuts and buttons | None |
| Fixed layouts | Docked, saved, resettable | None |
| Many floating windows | Only Preferences, Keyboard Shortcuts and About float; the ImGui demo is a debug panel | None |
| Inconsistent widgets | One property grid for components and scripts. The Renderer panel (`drawUI`) and Preferences use plain ImGui widgets; the editor's widgets are file-local in `inspector_panel.cpp` | UX-15, [#256] |
| Color-only information | Console lines carry `[warn]`, counts carry icons, axes carry letters, play carries a label. Instance members and engine content are only dimmed | UX-10 |
| Low-contrast themes | Body text 14.2:1 and dim text 8.7:1 are fine; two pairs fail | UX-10 |
| Hidden state | Tool, space, snap, icon visibility, play state and camera speed are shown. Ctrl inverting snap is not | UX-8 |
| Walls of text fields | Two-column grid, axis-colored strips, sRGB color pickers, angles in degrees, combos for enums | None |
| Blocking the UI during imports | Synchronous loading | [#260], UX-11 |
| "Are you sure?" | Delete and Remove are undoable without asking; only the unsaved-changes prompt asks | None |
| Over-customization too early | The style editor is a debug tool | Keeps presets and custom shortcuts deferred |

### ImGui-specific notes

| Note | State here | Action |
|---|---|---|
| Dockspace and DockBuilder default | Done | None |
| Layout persistence and Reset | Done, per project | Named layouts deferred (above) |
| Multi-viewport | Not used; `editor.md` lists it under Avoid (a swapchain per OS window, fragile with GLFW and Vulkan) | Agree. Revisit after [#254] (several views per frame), which a torn-off Game view would need anyway |
| Viewport panel | Done: offscreen `RenderView`, extent applied once the panel's size settles, UI textures released after the frames in flight | When next editing `viewport.cpp`: take the view's image from `FrameContext` instead of looking up the final image in the frame graph's declarations |
| Input routing | Done ([#280]) | None |
| Undo through the widget lifecycle | Done another way: every change applies live and merges into the open entry while the item is active; deactivation seals it. Same result, and the gizmo uses the same path | None |
| Stable IDs | Entities: `StableId` everywhere. Asset browser rows and folders use vector indices, so their open state moves to another row after a rescan or an import | UX-5 |
| Large lists | No `ImGuiListClipper` anywhere | UX-5 |
| High DPI and fonts | Done: content scale times UI scale, ImGui 1.92 dynamic fonts, Lucide merged into Inter | None |
| Idle behavior | The engine renders every frame and waits only while minimized | UX-16 |
| Model/view separation | Done: `aegis-editor-core` is pure and unit-tested, panels act through commands. Exceptions: the gizmo's direct write (UX-2), `drawUI` ([#256]) | UX-2 |
| UI testing | Headless editor tests (`-editor`, `-editor-default`, `-editor-play`, with screenshots) and editor-core unit tests; reference images planned ([#225]) | [#225] first. Dear ImGui Test Engine (not MIT, read its terms) only if interaction regressions (drag and drop, shortcut routing) slip past the screenshots |

### Build order

| Step | State |
|---|---|
| 1. Editor model, command and undo system | Done ([#165], [#201]); grouped entries missing (UX-1) |
| 2. Reflection and generic inspector | Done ([#133]); multi-edit missing (UX-12) |
| 3. Docked layout shell | Done ([#83], [#223], [#274]) |
| 4. Viewport panel | Done ([#200]); selection highlight missing (UX-8, UX-17) |
| 5. Outliner and asset browser | Done ([#202], [#273]); virtualized lists missing (UX-5) |
| 6. Command palette and shortcuts | Shortcuts done ([#223]); palette missing (UX-6) |
| 7. Background jobs and progress UI | Planned in M14 ([#259], [#260]); UI is UX-11 |
| 8. Autosave and crash recovery | Missing (UX-3) |
| 9. Polish: theme, icons, DPI, multi-viewport | Done except multi-viewport (deferred) |

**Dogfooding and scale.** `editor.md`'s "Trials" already runs the owner's workflows on Sponza, Helmets and Simple at
three resolutions and three scales. What no trial covers is scale: the stress scenes spawn their entities only when
played, Sponza's instance members make the largest edit world, and no project has a large asset folder.
UX-5 adds both.

## Plan items

### UX-1: Group several edits into one undo entry
**Here:** each `UndoStack` entry is one `Edit`, so deleting five entities, pasting several, or editing a field on
several entities would take as many undos. **Change:** an entry holds a list of edits, undone last to first and
redone first to last, with a title for the menu ("Delete 5 entities"); a scope in editor-core collects what the
commands record while it is open. Continuous merging stays within one entry. **Done when:** unit tests cover the
order, the title, a group that changes nothing (dropped), and the saved marker. **Size:** S, editor-core only.

### UX-2: One write path for components
**Here:** the inspector writes through `setField` and the storage's change hook (R1, D2's rule in one place); the
gizmo assigns `Transform` and calls `markStaticDirty` itself, and `instantiateAsset` sets the holder's location the
same way. **Change:** both write through the same registry write as the inspector (a component write with the change
hook in `core/inspector`). **Done when:** nothing under `ui/` calls `markStaticDirty`; a gizmo drag of a static entity
still moves it. **Size:** S; one issue with UX-1.

### UX-3: Atomic saves, autosave and recovery
**Here:** no autosave; `writeSceneFile` truncates the scene and writes it in place, so a crash or a full disk during
a save leaves a broken file; Play runs game code in this process. **Change:**
- *Atomic save:* write `<file>.tmp` next to the scene and rename it over the scene; mark saved only after the rename.
- *Autosave:* while the edit world is dirty, every few minutes (a preference) and on entering Play, write it
  (`sceneText`) to `<project>/.aegis/editor/autosave/<scene ID or "untitled">.scene`, never to the user's file;
  delete it on Save or Don't Save. Headless runs neither write nor read it, like the preferences.
- *Recovery:* at startup, an autosave newer than its scene file shows a non-modal notice in the status bar or above
  the viewport ("Unsaved changes to sponza.scene from 14:32: Restore, Discard"). Restore opens it as the scene,
  marked unsaved.

**Done when:** unit tests for the atomic write and the autosave's naming and staleness rule (editor-core); killing
the editor while playing and starting it again offers the changes. **Size:** M.

### UX-4: Multi-selection
**Here:** `Selection` keeps an ordered set and an active ID, and the status bar already says "and N more", but no
input adds to it. **Change:** in the hierarchy, Ctrl+click toggles and Shift+click selects the range between the
active row and the clicked one (the rows are a flat list, so a range is an index span); Ctrl+click toggles in the
viewport. Delete and Duplicate act on the selected roots (descendants of a selected entity are skipped) as one entry
(UX-1); dragging one of several selected rows moves them all; the gizmo moves all of them around the active entity's
pivot; Focus frames the union of their bounds. The inspector keeps showing the active entity, with "N selected", until
UX-12. **Done when:** unit tests for range selection over rows and for the selected roots. **Size:** M, after UX-1.

### UX-5: Lists that cost what they show
**Here:** `hierarchyRows` builds every row each frame and the panel draws them all (a million rows while playing
`stress/cubes`); the console copies and formats up to 2,000 lines each frame; the asset file list, the asset-slot
picker and All Entities draw every entry; asset rows and folders take vector indices as IDs. **Change:**
`ImGuiListClipper` over the flat rows in all five; the hierarchy rows cached and rebuilt when the hierarchy or the
open nodes change rather than each frame; the console's filtered lines kept until new lines arrive or the filter
changes; asset rows and folders identified by asset ID and path. To measure: an "Editor UI" profiler scope around the
shell's frame, and a generated edit scene of about 10,000 entities with some deep nesting. **Done when:** the PR shows
the Editor UI time before and after, playing `stress/cubes` and on the large scene. **Size:** M.

### UX-6: Command palette
**Here:** the `ActionRegistry` holds a label, chord, `enabled()` and `run()` for every menu action; Open Scene is a
modal with its own search. **Change:** Ctrl+K (Ctrl+P is Play) opens one search over actions (with their shortcuts,
disabled ones dimmed), the panels (to open), scenes (to open; File → Open… opens the palette on scenes, replacing the
modal), entities (to select and frame) and assets (to reveal in the browser), ranked by subsequence match and recent
use. **Done when:** unit tests for matching and ranking (editor-core); every menu action is reachable from it.
**Size:** M.

### UX-7: Context menus where they are missing
**Here:** hierarchy rows and component headers have menus; asset rows, the viewport and console lines don't.
**Change:** an asset row's menu: Open (a scene), Instantiate (under the selection), Copy ID, Copy Path. A right click
in the viewport that doesn't drag (the right button flies the camera) picks under the cursor and opens the hierarchy
row's menu for it. A console line's menu: Copy, and its link from UX-9. **Size:** S.

### UX-8: Selection, hover and snapping visible in the viewport
**Here:** a selected mesh shows only the gizmo, and nothing in Select mode; hover shows nothing; holding Ctrl inverts
snapping invisibly. **Change:** draw the selected entities' world bounds as a thin accent overlay in the viewport's
draw list, and the hovered entity's dimmer (the CPU pick, run only while the mouse moves); the snap button lights while
Ctrl inverts it. **Done when:** the `--editor-select` screenshot shows the bounds. **Size:** S. The real outline is
UX-17.

### UX-9: Log lines that lead to their subject
**Here:** `aegis::log` takes a format string and the console stores text, so "StaticMesh 0 has no name" can't take
the user to the asset. **Change:** an optional subject on a log message (an asset ID, an entity `StableId` or a file
path), set by a scope (a thread-local RAII object) in the importer, the asset library, the script system and the
editor's failed edits, so no call site changes its signature; sinks receive it, `ConsoleEntry` keeps it, the console
marks such lines, and a double click selects the entity or reveals the asset. **Done when:** Sponza's import warnings
lead to their assets; unit tests for subjects through the sink and the console buffer. **Size:** M; changes
`aegis.core`'s log API, so it can run in another lane than the editor items.

### UX-10: Contrast and cues beyond color
**Here:** light text on accent-filled controls (the Open button, the console's toggles when on, the active tool
buttons) is 2.8:1. `textDisabled` (`#808080`) also carries content that matters, such as instance members, engine
content and hints: 4.5:1 on the window, 3.8:1 on frames, 2.7:1 on a selected row. Instance members and engine content
differ from the rest only by that color. **Change:** fill accent controls with a darker token (`#2A5D9C` gives 5.4:1
with the text color), keeping `#4A8CD9` for lines, checkmarks and the selection tint; add a `textMuted` token near
`#9A9A9A` (6.3:1 on the window, 5.3:1 on frames) for secondary content and keep `textDisabled` for disabled controls;
add a lock icon to instance members and read-only roots. Write each pair's ratio beside its token. **Done when:** every
text and background pair the theme uses reaches 4.5:1, disabled controls excepted. **Size:** S. The trials still tune
the tokens; this sets their floor.

### UX-11: Background tasks in the editor (after [#260])
**Change:** a task list in editor-core (name, progress or indeterminate, cancellable) fed by the asset library's
load events; a status-bar indicator with the count and a popup listing the tasks with Cancel; "Loading…" in asset
slots while their asset loads. Feed #260's design checkpoint with what this needs: progress events, cancelling loads
when Open or New replaces the scene, and waiting for assets only in headless runs. **Size:** M.

### UX-12: Multi-edit with mixed values (after UX-4)
**Change:** the inspector shows the components every selected entity has; a field whose values differ shows a mixed
state (`ImGuiItemFlags_MixedValue` for checkboxes, a dash for numbers); an edit writes every entity as one entry.
**Size:** M.

### UX-13: Entity reference fields (with [#258])
**Change:** a `FieldKind` for an entity reference, stored as a `StableId` in the scene file; an inspector slot that
shows the entity's name, takes a dropped hierarchy row and offers a searchable picker; a duplicated or instantiated
subtree remaps references inside it to the copies. [#258] (scripts' lookup) needs the same reference. **Size:** M, world
and editor.

### UX-14: Field groups and advanced fields (with [#256])
**Change:** `FieldMeta` gains a group (a sub-header inside a component's section) and an "advanced" flag (shown
behind the section's toggle), and the inspector a field search once sections get long. Post-processing settings
([#256]) are the first component with enough fields to need it. **Size:** S.

### UX-15: Shared widgets (with the second user)
**Change:** move the value widgets (axis strips, sRGB color, angle, rotation, asset slot) from `inspector_panel.cpp`
into the `ui/widgets` partition `editor.md` planned, once a second panel needs them (native material files [#141],
project settings), and use them in Preferences. **Size:** S.

### UX-16: Idle throttling
**Change:** the shell tells the engine when it is idle (not playing, no input for about half a second, no drag), and
the engine then waits for events with a timeout through `platform::Window` instead of polling; a lower rate while the
window is unfocused. Background loads ([#260]) wake it. **Done when:** an idle editor's GPU use drops near zero and
interaction is unchanged. **Size:** S to M, engine loop and editor; windowed check on a real GPU. Low priority on a
desktop, more on a laptop.

### UX-17: Exact picking and a selection outline (render lane, after [#254])
**Change:** an entity-ID attachment for the editor's view, read back on click (exact, and right for the GPU-driven
path), and an outline pass for the selected set; the `RenderView` carries the set, so the editor still names no
render internals. **Size:** L, `needs-design` (where editor overlays live in the graph).

## Proposed issues and order

All are `area/editor` unless noted, so they run one at a time.

| Order | Items | Issue | Size | Blocked by |
|---|---|---|---|---|
| 1 | UX-3 | Save scenes atomically, autosave and offer recovery | M | |
| 2 | UX-1, UX-2 | Group edits into one undo entry and write components through one path | S | |
| 3 | UX-4 | Select several entities in the hierarchy and the viewport | M | 2 |
| 4 | UX-5 | Clip the editor's long lists and measure a large scene | M | |
| 5 | UX-10 | Raise the theme's contrast and mark read-only content | S | |
| 6 | UX-8 | Show the selection, hover and snapping in the viewport | S | |
| 7 | UX-7 | Add context menus to assets, the viewport and console lines | S | |
| 8 | UX-6 | Add a command palette over actions, scenes, entities and assets | M | |
| 9 | UX-9 | Give log messages a subject and link console lines to it (`aegis.core` and editor) | M | |
| with others | UX-11 | Show background tasks in the editor | M | [#260] |
| with others | UX-12 | Edit shared fields of several entities | M | 3 |
| with others | UX-13 | Add entity reference fields | M | with [#258] |
| with others | UX-14 | Group fields and hide advanced ones in the inspector | S | with [#256] |
| with others | UX-15 | Share the inspector's widgets | S | with [#141] |
| Backlog | UX-16 | Throttle the editor while idle (app and editor, `needs-gpu`) | S | |
| Backlog | UX-17 | Pick by entity ID and outline the selection (render, `needs-design`) | L | [#254] |

UX-3 goes first: it is cheap and guards everything after it. UX-9's log change can run in another lane while an editor
item is in progress.

[#83]: https://github.com/chFleschutz/aegis-engine/issues/83
[#133]: https://github.com/chFleschutz/aegis-engine/issues/133
[#141]: https://github.com/chFleschutz/aegis-engine/issues/141
[#142]: https://github.com/chFleschutz/aegis-engine/issues/142
[#154]: https://github.com/chFleschutz/aegis-engine/issues/154
[#165]: https://github.com/chFleschutz/aegis-engine/issues/165
[#166]: https://github.com/chFleschutz/aegis-engine/issues/166
[#200]: https://github.com/chFleschutz/aegis-engine/issues/200
[#201]: https://github.com/chFleschutz/aegis-engine/issues/201
[#202]: https://github.com/chFleschutz/aegis-engine/issues/202
[#223]: https://github.com/chFleschutz/aegis-engine/issues/223
[#225]: https://github.com/chFleschutz/aegis-engine/issues/225
[#254]: https://github.com/chFleschutz/aegis-engine/issues/254
[#256]: https://github.com/chFleschutz/aegis-engine/issues/256
[#258]: https://github.com/chFleschutz/aegis-engine/issues/258
[#259]: https://github.com/chFleschutz/aegis-engine/issues/259
[#260]: https://github.com/chFleschutz/aegis-engine/issues/260
[#272]: https://github.com/chFleschutz/aegis-engine/issues/272
[#273]: https://github.com/chFleschutz/aegis-engine/issues/273
[#274]: https://github.com/chFleschutz/aegis-engine/issues/274
[#280]: https://github.com/chFleschutz/aegis-engine/issues/280
[#283]: https://github.com/chFleschutz/aegis-engine/issues/283
