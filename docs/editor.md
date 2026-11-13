# Editor: architecture, features and implementation plan

Status: **in progress.** E1 ([#83]) is merged; the [M10 milestone][m10] tracks the rest. It plans the
editor that M9 (D7) splits off and M10 builds.
It builds on [`editor-runtime-split.md`](editor-runtime-split.md) (the launcher, the type registry, play-in-editor),
[`asset-system.md`](asset-system.md) (D8: asset IDs, scene files, `World` vs `Scene`) and
[`engine-architecture.md`](engine-architecture.md) (services, `Presentation`). It fixes how the editor is
built, what it does in M10, and which runtime features have to exist first.

Plan items are numbered here so the issues can refer to them: **R1–R14** are runtime prerequisites,
**E1–E9** are editor steps. **Each step's scope lives in its issue**; this document holds the design
the issues cite and links them, and doesn't repeat their content lists.

## Goals

- The editor is a library that only `<Game>-Editor` links. The runtime has no editor code, no `#ifdef`
  and no editor hooks beyond one generic interface (R4).
- Game types need no editor code: the inspector, the Add menus, undo, copy and play-in-editor all go
  through the type registry and the serializer.
- Each frame the editor reads and writes the world at one safe point, never during rendering.
- The logic is testable without a GPU or ImGui. Commands, undo, selection, the property model and the
  play session are pure code with doctest coverage. The ImGui layer stays thin.

## Status

The code is the reference for the current state; this section only says which plan items are done.

- **Done:** E1 ([#83]: R3, R4, R8; the action registry, theme, fonts and Reset Layout came with E8, [#204]), E2 ([#200]), E3 ([#133]), the field half of E4 ([#165]: `undo_stack`, Ctrl+Z / Ctrl+Y, the Edit menu), E6 ([#202]: the Assets panel; opening a scene doesn't ask about unsaved changes until [#166]), R1 and R2 ([#191]), R10 ([#196]), R12 ([#198]), R13 ([#199]), R14 ([#132]).
- **Open:** every other R- and E-item, each in its issue (see "Runtime prerequisites" and
  "Implementation plan").

The editor started from about 900 lines of legacy code attached as a `core::Layer`, with its UI built
inside the render graph, selection by `world::Entity` and hand-written type lists. E1 replaced the
layer with the shell, moved the UI to the safe point and keyed the selection by `StableId`; E3 ([#133])
replaced the hand-written type lists and `ComponentTraits` with the type registry.

## Architecture

### Libraries and dependency direction

```
<Game>-Editor  (launcher, editor flavor, #132)
 └─ aegis-editor        ImGui layer: EditorShell, panels, widgets, theme, gizmos   (imgui, imguizmo)
     └─ aegis-editor-core   pure model: EditorContext, selection, commands + undo,
                            property model, hierarchy model, play session, editor camera math
         └─ runtime: aegis-engine (Engine, Presentation, Shell), aegis-world, aegis-reflect,
                     serializer (#131), aegis-asset-library / aegis-assets, aegis-core
```

- **Dependencies only point down.** No runtime library imports `aegis.editor*`. A game's own editor
  code (custom drawers, later) sits next to the game module and links `aegis-editor`, only into
  `<Game>-Editor`.
- **`aegis-editor-core` never includes ImGui and never touches the GPU.** It gets its own test
  executable, `aegis-tests-editor`, which links only that library. The world it edits is a plain
  `World` (no device), exactly as in `aegis-tests-world`.
- **The editor reaches the runtime only through public APIs:** `World` / `Registry`, `TypeRegistry`
  plus `ComponentStorage` (R1), the serializer (R9), `AssetLibrary` / `AssetDatabase` (R10), the
  `Shell` hooks and render views (R4, R5), and read-only introspection (`FrameGraph`, `Profiler`). It
  never names `vk::` types. ImGui textures come from `Presentation` (R6).
- **New code style.** The editor is rewritten, so it uses the new style: a `.cppm` interface plus a
  `.cpp` implementation, partitions, `std::expected` and `[[nodiscard]]`. ImGui and ImGuizmo headers
  appear only in `.cpp` files, which keeps module interfaces free of third-party headers (AGENTS.md,
  "keep std-using headers out of interface fragments") and stops a panel edit from rebuilding its
  importers. The style table in AGENTS.md moves `editor/` to the new-style column once E1 lands.

### Module layout

```
src/aegis/editor/
├─ core/                          aegis-editor-core, module aegis.editor.core (no ImGui)
│  ├─ context          EditorContext: project, edit/play worlds, selection, undo stack, play state
│  ├─ selection        set of StableIds + the active one; drops entities that no longer exist
│  ├─ commands         Command interface; SetField, Rename, Create/Destroy/Duplicate entity,
│  │                   Add/Remove component, Attach/Detach script, Reparent
│  ├─ undo_stack       history, merging of continuous edits, the saved marker (dirty flag)
│  ├─ property_model   TypeInfo/FieldInfo + metadata -> rows {label, widget, limits, unit, readOnly}
│  ├─ inspector        the components and scripts an entity shows and can add; field writes + change hook
│  ├─ hierarchy_model  entity tree as rows in sibling order, with the name filter; pure
│  ├─ play_session     Edit -> Playing <-> Paused -> Edit, step
│  ├─ editor_camera    fly/orbit/pan/dolly/focus math -> view, projection and picking rays
│  ├─ picking          ray against bounding spheres, icon hits in screen space
│  └─ settled_extent   the viewport's render size, applied once the panel's size settles
└─ ui/                            aegis-editor, module aegis.editor (ImGui in .cpp only)
   ├─ editor_shell     implements Shell; owns the context and the panels
   ├─ actions          action registry: id, label, shortcut, enabled(), run()
   ├─ theme            colors, sizes, fonts and icons in one place (theme.h: tokens)
   ├─ widgets          property grid, one widget per FieldKind, asset slot, axis-colored vec3
   ├─ viewport         scene view: editor camera input, ImGuizmo inside the image rect, icons, picking
   └─ panels/          hierarchy, inspector, assets, console, renderer, profiler,
                       statistics, project settings
```

`Panel` is a small interface (`name()`, `draw(EditorContext&)`, `open` flag). Panels never call each
other: they share state only through `EditorContext` (selection, active world, play state) and act
through commands and actions. Menus, toolbar buttons and keyboard shortcuts all trigger the same
`Action`, so each behavior is written once (and a command palette can list the actions later).

### The runtime-to-editor contract: `Shell`

The engine has one extension point (R4, [#83]). A **shell** decides, each frame, what the app shows around
the game: its UI, which worlds update and render, and who receives input. The launcher picks it: the
runtime flavor keeps `GameShell` (the engine's default), the editor flavor installs `editor::EditorShell`
with `Engine::setShell` before `run()`. `Shell`, `GameShell` and `FrameContext` live in `aegis.engine`,
next to `Engine` (`src/aegis/engine.cppm`).

```cpp
class Shell
{
public:
    // Once Engine::run starts: after the project, the game's types and its startup scene have loaded.
    virtual auto onStartup(Engine& engine) -> std::expected<void, EngineError>;
    // The safe point: input has been read and the ImGui frame is open; no world is being updated or rendered.
    // frame.update(world, mode) picks the worlds that update this frame: Play runs every system, Edit only
    // those that opt in (System::runsInEdit), R8. frame.render(world) picks the one that renders.
    virtual void onFrame(FrameContext& frame) = 0;
    // The input the game doesn't see this frame, asked before the input is read.
    virtual auto inputCapture() const -> InputCapture;
    // First in the engine's teardown, while the worlds, the frame graph and ImGui exist.
    virtual void onShutdown();
};
```

With several worlds (R7, [#194]) the shell decides which worlds tick, each by its own clock: `GameShell`
the main world in `Play`, `EditorShell` the edit world in `Edit` or the play world in `Play`. A world it
doesn't update stands still, clock included, and begins with its first update. The main world renders
unless the shell renders another.

Still to come: the views to render (world, camera and target per view, R5 [#192]) and a close veto
(`canClose`, on the window's close request from [#198], with E5 [#166]).

### Frame loop (owned by `Engine::run`)

```
1. platform     window events; input update. Capture flags come from the shell (viewport focus), not raw ImGui flags;
                the frame graph follows the RenderView extent the shell asked for last frame
2. UI + edits   Presentation: ImGui NewFrame
                shell.onFrame(frame)    <- the editor builds its UI and applies edits directly (safe point)
                Presentation: ImGui::Render() -> draw data ready
3. simulate     the worlds the shell updated in onFrame: deferred commands, then the phases
4. render       for each RenderView: render world W through camera C into target T;
                the UI pass then records the ready draw data onto the swapchain image
5. present
```

- **Why edits come before the update:** an edited `Transform` propagates (TransformSystem) and renders
  in the same frame. If the edit came after the update, the frame would show the old state, and a
  static entity marked dirty would wait a frame.
- **Why this is safe:** frames in flight hold GPU copies only. The passes read the registry while the
  frame graph records (step 4), so between steps 2 and 4 nothing reads CPU state. `World::defer` stays
  for scripts and systems, but the editor no longer needs it.
- **What changes in the render graph (R3):** `UiPass` stops calling the build callback and only records
  the draw data. In headless mode `Presentation` sets `io.DisplaySize` to the target size and runs no
  platform backend. That gives a headless editor run with a screenshot (E9's smoke test and the design
  review in E8).

### Worlds and play-in-editor

- **Edit world:** instantiated from the open scene file. It never runs scripts. In `Edit` mode only the
  systems that opt in run (TransformSystem, CameraSystem), so moved entities update and nothing plays.
- **Play world:** Play captures the edit world with `serialize::saveScene` (in memory, no text) and
  instantiates that `Scene` into a new `World` (`Engine::createWorld`), which gets the engine's and the
  game's systems (R7) and its own clock, and begins with its first update in `Play`. Stop destroys it. Pause stops updating it; Step runs one fixed-step
  frame. Instances come back through their holders' `SceneInstance`, as they do when a file loads.
- **Identity across play:** a world's root scene keeps its local IDs (D8), so the play world's
  entities have the edit world's `StableId`s. Selection survives Play and Stop unchanged.
- **Edits during play** go to the play world and are lost on Stop, as in Unity. They don't enter the
  edit world's undo stack (the play session keeps its own and drops it). The UI is tinted while
  playing, so nobody mistakes the play world for the edit world.
- **Instances per world:** each world gets its own `InstanceStore` and `InstanceBridge` ([#194]).
  Play and Stop switch which world's `FrameScene` the frame graph renders. One world renders at a
  time in M10; edit and play views side by side need two views in one frame.

### Viewport and the editor camera

- **The scene renders into a viewport texture (R5), not the swapchain.** The texture is sized to the
  viewport panel, and the graph resizes when the panel size has been stable for a few frames, to avoid
  thrashing during a drag. `ImGui::Image` shows it through a UI texture ID (R6). The UI pass declares
  a read of the viewport texture, so the graph orders the scene passes first. The runtime flavor keeps
  rendering straight to the swapchain, with no extra copy.
- **The editor camera belongs to the editor, not the world.** It has a position, yaw/pitch, FOV,
  near/far and a speed. It is never an entity, so it can't end up in a scene file or be moved by a
  script. Its `RenderView` replaces the `FrameScene`'s camera that `extractFrameScene` takes from the
  world. In play mode the viewport shows
  the world's main camera, and "eject" switches back to the editor camera while playing (nice to have).
- **Controls:** RMB + WASD/QE to fly (wheel changes the speed, which the speed field shows), Alt + LMB to orbit,
  MMB to pan, wheel to dolly, F to focus the selection (its bounding sphere). Z is up, matching D8.
- **Overlays** ([#325]): the tool buttons sit in the image's top left corner. The top right holds the camera speed,
  a field marked with a gauge that drags to change it (logarithmically, 0.01 to 1000 units per second) and takes a
  typed value on a click, and the stats toggle. Below them the navigation gizmo shows the world axes in the camera's
  orientation; the positive ends are discs labelled X, Y or Z, the negative ends dim rings without a label (it shows
  while one is hovered, as in Blender), and a click views the pivot from that side, at the same
  distance (the clicked end then points at the viewer; from straight above or below the heading stays). While the
  toggle (kept in the preferences) is on, the stats overlay below the gizmo shows the Statistics panel's summary:
  entities, draw batches and instances. Every overlay is an ImGui item, so a click on one neither picks nor
  starts a camera drag; they scale with `theme::scale()`, stay in full viewport and are hidden in play.
- **Gizmos:** ImGuizmo, drawn into the viewport window's draw list and clipped to the image rect. It has
  translate, rotate and scale, a local/world toggle, and per-mode snap values from the editor
  preferences (Ctrl inverts snapping, and the snap button lights while snapping is in effect). W/E/R/Q work only while the viewport is focused. One drag is one
  undo command: the transform is captured when the drag starts and committed when it ends.
- **Picking:** at first a CPU ray against each `MeshRenderer`'s world-space bounding sphere
  (`StaticMesh::bounds()`), nearest hit, with Alt-click cycling through overlapping hits. Lights and
  cameras get screen-space icons drawn with the viewport draw list, and those are clickable too, each within
  its drawn radius. A light's icon takes the light's color; icons farther than 10 units shrink and fade, down
  to 40% of the size and 35% of the opacity at 40 units, and the selected one draws on top. The toolbar's eye
  button hides them (hidden icons can't be clicked), remembered in the preferences. Corner brackets show what
  a click picks by, the projected bounding sphere or else the icon: accent around the selected entities (the
  active one's stronger), dim around the one under the cursor, which is picked again when the cursor moves with
  no button held. A sphere reaching behind the camera shows none. A GPU ID buffer is a later improvement (nice
  to have).
- **Input routing:** the editor camera gets input while the viewport is hovered or RMB is held. The
  play world gets game input only while the viewport is focused in play mode; it shows "Shift+F1 releases
  the input" meanwhile. Shift+F1 drops the viewport's focus and shows the cursor again, and a click on the
  viewport gives the input back. The shell sets the capture flags: ImGui's, plus everything while the
  viewport is hovered or the camera is being dragged. While the game has the input, only chords with a
  modifier run in the editor (plain keys are the game's), and the key of one is kept from the game from
  the frame it is pressed (the engine asks for the capture again after the shell's frame) until it is
  released. Holding Ctrl alone still reaches the game, which may use it (the movement controller's slow
  walk).

### Reading and modifying data (the technical UI side)

The flow is one-way: **panels read the world through the registry, and every change goes through a
command.**

```
Inspector (ImGui)          editor-core                       runtime
─────────────────          ───────────                       ───────
for each component type t  property_model(t, meta)    <-    TypeRegistry: TypeInfo, FieldInfo, FieldMeta (R2)
  the entity has:          -> rows {label, widget,          ComponentStorage attachment: find/add + remove,
                                                            changed (R1)
  draw rows                   value = getter(ptr)}
widget edited  ──────────> edits.begin / change / commit
                           ─ begin:  remember old Value
                           ─ change: apply live (setter + changed hook, R1), no history entry
                           ─ commit: push SetField{id, type, field, old, new} (merged)
Ctrl+Z  ─────────────────> undo: SetField applies old ───>  setter + changed hook
```

- **Entity references are `StableId`s** in every editor structure: selection, commands, ImGui IDs
  (`PushID` on the 64-bit ID). The `world::Entity` is looked up at use, so a command still works
  after its entity was destroyed and recreated by undo (`Registry::createWithId`).
- **Widgets map from `FieldKind` plus metadata (R2):** Bool → checkbox; Int/UInt → drag (or a slider
  with a range); Float/Double → drag with a step, and the `Angle` hint shows radians as degrees;
  Vec2/3/4 → axis-colored drags, and the `Color` hint shows a linear color picker that edits in sRGB
  (D8); Quat → Euler degrees; Enum → combo from the `EnumEntry` list; String → text field (edits
  commit on deactivate); AssetRef → an asset slot that shows the asset's kind icon and short name (a
  file's name without folder and extension, a part's key without the type prefix: `assetDisplayName`;
  the full path in the tooltip), opens a picker of the field's asset type (a search above the list,
  Enter picks the first match, the assets by name with their file's name, "None" first), accepts drops
  from the browser. The mapping is a pure
  function in `property_model`, unit-tested ([#133]'s "field-to-widget mapping").
- **Quaternions in the inspector:** converting quat → Euler every frame makes a drag jump at the
  gimbal boundaries. While a rotation field is being dragged, the property model keeps the Euler
  angles it started from and derives the quaternion from them. It re-reads the quaternion only when
  nothing is active.
- **Change hooks (R1):** after a write, `ComponentStorage::changed(registry, entity)` runs. For
  `Transform` it calls `markStaticDirty` on non-dynamic entities (D2's rule, now in one place instead
  of in each editor widget). For `MeshRenderer` it calls `invalidateStatic`. The editor stays generic.
- **Entity properties and required components:** the inspector shows name and ID (read-only, with a
  copy button) as the entity's header. Only registered components get sections, so GlobalTransform,
  the hierarchy links and StaticDirtyTag never appear. Type metadata (R2) marks `Transform` as
  `required` (no remove button, since `Registry::create` always adds it). That replaces
  `ComponentTraits`. Tag types (DynamicTag, MainCamera) are components with no fields: they show a
  header without a body.
- **Scripts:** a "Scripts" section lists the entity's `ScriptEntry`s, each with its registered fields
  through `ScriptEntry::object`. The one "Add..." popup lists the registry's script types after the components. Attach
  and detach are commands.
- **Instance members are read-only** until overrides exist ([#142]). Entities created by a scene
  instance carry `InstanceMember`, and `saveScene` leaves them out, so an edit to them would be lost
  without warning. The inspector shows them disabled, with a note naming the instanced scene, and the
  hierarchy shows instances with their own icon. The holder (with its `SceneInstance` and
  `Transform`) stays editable.
- **Custom drawers are the exception.** The generic drawer plus metadata covers every built-in type.
  A drawer registry keyed by type name (editor-side) exists for the few that need more (a future
  `SkinnedMesh`, curves). A game registers drawers from its editor-only sources. Nothing in the
  runtime knows about them.
- **Reset and copy:** a right click on a component or script header opens a menu of Reset (the values of a default instance, `TypeInfo::create`), Copy and Paste (field values as one JSON object
  of `toText` text on the system clipboard, in `core/component_text`; a paste only applies to the same type and skips
  read-only fields) and Remove (Detach for a script). Reset and Paste are one `ComponentEdit` each, so one undo
  reverts them. An entity's ⋮ menu beside its name offers Copy ID; the ID otherwise shows in the name's tooltip.
  The "Add..." popup's search field filters components and scripts by name and category; Enter adds the first match.
- **Structural edits are commands too:** Create, Destroy and Duplicate store the entity's subtree as a
  `serialize::Scene` snapshot (R9), so undo restores it with its IDs and Duplicate gives the copy new
  ones.
  Add and Remove component store the component's field values. Reparent stores the old parent and the
  sibling index (R11).
- **Undo stack:** commands with `apply` / `revert` on a `World`, merged by key (same entity, type and
  field, within one interaction), with a saved marker. The dirty flag is "position != saved". It is
  pure and tested on a device-less world ([#165]). Edit → Undo and Redo name the edit they would take
  ("Undo Move 'Helmet'", `editTitle` in `core/edits`; [#281]).
- **Hierarchy:** rows come from `Parent` / `Children` / `Siblings`, walked iteratively (as today),
  with a name filter (matching rows keep their ancestors). Drag and drop reparents and reorders,
  rejecting cycles (`Registry::isInSubtree`). F2 renames, Del deletes, Ctrl+D duplicates, F frames the entity. One Create menu (Empty, then
  an entity of each creatable component type, by category a separator apart, each with its icon and no submenus, so
  anything is one click away; one undo entry each, the new entity selected and named at once) sits behind the "+"
  button beside the filter and in the Entity menu, and makes up the window's context menu; a row's context menu
  selects the row first and offers Rename, Duplicate, Delete and Focus, named after the action registry, with the
  Create menu's items below them, which create a child of the row (not for a scene instance's members). "All Entities", a flat list of every entity, is a
  debug panel.
- **Asset browser (R10):** a folder tree over `AssetDatabase::entries()` with two roots, the project
  and the engine content (read-only, shown dimmed; Lucide `folder-root` and `package`, folders `folder` and
  `folder-open`). Right of it, a breadcrumb of the open folder's path (each folder a button, an Up button
  before them) over its contents in one of two views, picked in the toolbar: a list, each file's sub-assets
  expandable below it, or a grid of tiles with large icons and names on up to two lines, a model's
  sub-assets shown after it (filled) by a button at its tile's edge. Every line carries its kind's icon in
  its kind's color (the theme's asset icon tokens); a click selects it, a double click enters a folder,
  opens a scene file or shows a model's parts. Search and a type filter cover both views. A drag carries an `AssetId` payload with its type:
  dropping it on an asset slot assigns it (type-checked), and dropping a `Scene` or model on the
  viewport or hierarchy instantiates it (`AssetLibrary::instantiateScene` with a parent; models
  become scenes in [#140]).
  A Rescan button covers the time before the file watcher
  ([#143]).
- **Scene files:** New, Open (a picker over the database's `Scene` entries), Save (Ctrl+S), and Save As
  (an in-editor path field inside the project, which writes a new native file with a new ID and adds
  it to the database). The title bar and the scene tab show `*` while dirty. Open, New and closing the
  window ask about unsaved changes (`Shell::canClose`, R12). There are no native file dialogs. A save
  writes `.<name>.tmp` next to the scene and renames it over the scene, so a failed save leaves the
  last good file ([#307]).
- **Autosave and recovery** ([#307]): while the edit world is dirty, every few minutes (a preference,
  0 turns it off) and on entering Play, the editor writes it to
  `<project>/.aegis/editor/autosave/<scene ID>.scene` (`untitled.scene` for a scene never saved), never
  to the scene's file. Save, Save As and Don't Save delete it. At startup and when a scene opens, an
  autosave newer than the scene's file shows a bar above the viewport: Restore opens it as the scene,
  unsaved, with an empty history; Discard deletes it; an edit hides the bar. Headless runs neither
  write nor read autosaves.
- **Console:** a log sink (R13) feeds a ring buffer with level filters and counts, search, clear, copy and a
  Collapse toggle that merges repeated messages, while stderr output continues. The status bar's Console button
  carries the warning and error counts in the same button while there are any, with the last problem in its tooltip
  ([#279], [#322]).

### Persistence

- **Layout:** `imgui.ini` goes in `<project>/.aegis/editor/imgui.ini` (per project, per machine, never
  committed, D8's one ignored folder), with the pinned drawer panels in its `[Aegis][Drawers]` section and the
  folded side column in `[Aegis][Rail]`. A View → Reset Layout action rebuilds the default with `DockBuilder`.
- **Preferences** (camera speed, snap values, the viewport icons' and stats' toggles, recent projects, UI scale, the autosave interval, the drawer's height, full viewport on play) go in the user config folder
  (`%APPDATA%/Aegis/editor.json`, `$XDG_CONFIG_HOME/aegis/editor.json`), as JSON through glaze in a
  `.cpp`. The Preferences window doesn't list the recent projects until something opens them (a welcome
  screen or a project switcher).
- **Recent scenes** (File → Open Recent: the last 10 opened or saved, most recent first, missing ones
  dropped) are per project, in `<project>/.aegis/editor/recent_scenes.json` ([#281]).
- **Nothing editor-only goes in scene files.** That covers the editor camera, selection, expanded tree
  nodes and gizmo mode. The camera per scene can be remembered in `.aegis/editor/` (nice to have).

## Features

### Must have (M10: a usable editor)

1. **Editor only in the editor executable**, with the runtime executable free of it ([#83]).
2. **Docked default layout,** persisted, with Reset Layout.
3. **Viewport panel** with an offscreen scene texture, editor camera (fly, orbit, focus) and resize.
4. **Hierarchy:** tree, selection, create/destroy/duplicate, rename, drag-and-drop reparenting and
   reordering, name filter.
5. **Inspector generated from the registry,** with metadata-driven widgets, scripts and their fields,
   and the Add menu ([#133], [#275]).
6. **Transform gizmos** (translate/rotate/scale, local/world, snapping), undoable.
7. **Click picking** in the viewport, plus icons for lights and cameras.
8. **Undo/redo of every edit:** fields ([#165]), and structural edits (a new item).
9. **Scene New/Open/Save/Save As,** dirty indicator, unsaved-changes prompt ([#166]).
10. **Play/Pause/Step/Stop** with a separate play world that is discarded on Stop ([#166]).
11. **Asset browser:** browse, search, drag to asset slots and into the viewport, instantiate models
    and scenes.
12. **Console** with level filters.
13. **The existing debug panels** (renderer passes and resources, CPU/GPU profiler, statistics) under a
    Debug menu.
14. **Editor preferences** (camera speed, snap values, UI scale) and high-DPI scaling.
15. **A headless editor smoke test** ([#205](https://github.com/chFleschutz/aegis-engine/issues/205)).

### Nice to have (after M10, mostly Backlog)

- Multi-selection with multi-edit (fields that differ shown as mixed) and a shared pivot for gizmos.
- Copy and paste of entities across scenes, with the clipboard holding a scene fragment as JSON.
- Selection outline and GPU ID-buffer picking (exact, works for the GPU-driven path).
- Grid and debug drawing (light ranges, camera frusta, bounds). This needs a debug-line pass in the render graph.
- Render debug views (G-buffer channels, wireframe, overdraw), an orthographic editor camera and a view menu for them.
- Asset thumbnails (textures directly; meshes and materials through an offscreen preview render).
- Material editing, with native material files and "Extract material" ([#141]).
- Asset hot reload ([#143]); prefab overrides, unpacking and an "edit prefab" mode ([#142]).
- A separate Game panel next to the Scene viewport while playing (needs two views in one frame).
- Project settings panel (startup scene, window defaults), recent projects, and a welcome screen when
  there is no `--project`.
- Command palette (Ctrl+Shift+P) over the action registry; user-defined shortcuts.
- Camera bookmarks; the editor camera remembered per scene.
- Render-pass settings through the type registry instead of `RenderPass::drawUI()`.
- UI automation tests with Dear ImGui Test Engine (check its license first; it is not MIT).

### Avoid

- **Editor code in runtime libraries:** no `#ifdef AEGIS_EDITOR`, no editor-only fields on
  components, no editor entities (camera, gizmo helpers) in the world, nothing editor-only in scene
  files.
- **Hand-written per-type UI** and parallel type lists (`ComponentTraits`, Add menus written out by
  hand). Use metadata first, and a custom drawer only when metadata cannot express the field.
- **Keeping `world::Entity` / `entt::entity` across frames** in editor state. Use `StableId`.
- **Writing to the world from inside the render graph** or during rendering, and `World::defer` as the
  editor's write path.
- **The editor touching Vulkan,** the ImGui Vulkan backend or render-graph internals, and new
  `drawUI()` hooks in passes.
- **Whole-world snapshots per edit** for undo. Field edits store two values; structural edits store
  only the affected subtree.
- **ImGui multi-viewport** (panels as OS windows): it multiplies the swapchain and backend work and is
  fragile with Vulkan and GLFW. Same for **native file dialogs**, a new platform dependency the asset
  database makes unnecessary.
- **Our own widget toolkit** or a retained-mode UI framework, and heavy theming before E8 has tested
  the workflows.
- **Running game scripts in edit mode,** or giving the edit world the game's input.
- **Editing engine content** (it is read-only) and writing anything into the project except the files
  the user saves (scenes, later materials) and the sidecars the database already writes.
- **Out of scope for M10:** node editors and visual scripting, terrain and foliage tools, an animation
  timeline, in-editor code editing, a build/cook UI (cooking is the CLI, [#134]).

## UI design

Decided in E8 ([#204]) and built: the theme is `ui/theme.h` (the tokens), `ui/theme.cppm` (the icons) and `ui/theme.cpp` (the
fonts and the style), the layout is `EditorShell::drawDockspace`. What the owner's workflow and scale trials show (below) tunes the
tokens, not this structure.

### Layout

```
┌────────────────────────────────────────────────────────────────────────────────────┐
│ File Edit Entity Play View Debug Help      ▶  ❚❚  ⏭                ◫ ⤢ sponza.scene* │ menu bar + play controls + scene
├──────────────────────────────────────────────────────────────┬─────────────────────┤
│ ↖ ✥ ⟳ ⚖ │ ▢ ⌖ ◉ │                            [◷ 5.0 u/s] ▥   │ ☰ HIERARCHY         │ viewport overlays
│ ┌──────────────────────────────────────────────────────────┐ │ Filter           [+]│
│ │                                                    Z     │ │ ▾ Sponza            │
│ │                 Viewport (scene texture)        Y─┼─X    │ │   ▸ Helmet          │
│ │                          gizmo                           │ ├─────────────────────┤
│ ├──────────────────────────────────────────────────────────┤ │ ⚙ INSPECTOR         │
│ │ 📁 ASSETS │ >_ CONSOLE                              📌  ✕ │ │                     │ the drawer, while open:
│ │ Rescan  All types  Search                                │ │                     │ over the viewport
│ └──────────────────────────────────────────────────────────┘ │                     │
├──────────────────────────────────────────────────────────────┴─────────────────────┤
│ 📁 Assets >_ Console ⚠ 25  Sponza  Q select W move…       60 fps | CPU 4.2 | GPU 4.1 │ status bar
└────────────────────────────────────────────────────────────────────────────────────┘
```

- **Default split:** one column on the right over the full height, the hierarchy on top (40% of its height) and
  the inspector below ([#321]), and the viewport in the rest: there is no bottom dock ([#322]). The column is
  360 px wide times `theme::scale()`, at most 30% of the window, and keeps its width when the window is resized:
  ImGui gives the change to the viewport, the central node. Built with `DockBuilder` on first start and by
  View → Reset Layout, which also reopens the panels that start open, unpins the drawer panels, closes the drawer and
  unfolds the column.
  The layout is kept in `<project>/.aegis/editor/imgui.ini`. A change to the default doesn't replace a kept layout:
  View → Reset Layout builds the new one. (A bottom node left from an older default stays hidden while nothing is
  docked in it.) A panel's tab shows its icon and its title in capitals (`Panel::label`); ImGui identifies the
  window by `###<title>` (`windowName`), which the layout file and every lookup by name use. A layout kept before
  the labels names the windows by their old titles: Reset Layout docks them again.
- **Drawer** ([#322]): Assets, Console and the debug panels open in a drawer instead of a dock node, as Unreal's
  Content Drawer does. It is a window over the bottom of the viewport and as wide, so it covers the image instead of
  shrinking it (the render extent stays) and the right column stays usable (drag a material from the drawer onto an
  inspector slot). Its height is a share of the viewport's, dragged at its top edge and kept in the preferences. It
  shows one panel at a time with no header (the status bar's lit button names it), its pin button over the right end
  of the panel's first row. The status bar's buttons, their chords, the View menu and the Debug menu open it; Esc,
  the chord again or a click anywhere outside it (but in its popups and the status bar) close it; losing the focus
  alone doesn't, since drag and drop takes it. A drag that leaves the drawer closes it, and its drop still lands in the
  viewport or on an inspector slot (ImGui keeps the payload while the button is held). There is no animation.
- **Pinned drawer panels** dock into a node below the viewport, split off when the first one is pinned (or the one
  a panel was pinned in before), as tabs; a lit pin button over the right end of their first row returns them to
  the drawer. Like any docked panel they can be dragged to another node; one that appears floating (pinned in the
  layout file without a node, or dragged out before) docks below the viewport again. The pinned panels are kept
  with the layout. `--editor-panels all` pins every drawer panel, so the
  `game-<name>-editor` screenshot shows them.
- **Dock chrome:** the viewport is the central node and has no tab bar (it can't be closed); no node shows the
  window menu or the close-all button, each tab has its own close button.
- **Debug panels** (Renderer, Profiler, Statistics) start closed; the Debug menu shows them in the drawer, or brings a pinned one to the front (they
  are wide tables) and lists the ImGui demo and the style editor (`ImGui::ShowStyleEditor`, for live tuning).
- **Status bar:** the Assets and Console buttons at the left, each its icon and name (Lucide `folder` and `terminal`,
  lit while the drawer shows their panel), the console's warning and error counts inside the Console button while
  there are any, then, a slim line apart, the selection. The bar is a frame taller by an inset above and below its buttons. The middle shows the keys for what the cursor is over, each key with what it does
  ("Q select W move E rotate R scale"): the gizmo and camera keys over the viewport, the fly keys while the right mouse
  button is held, F2 and drag to reparent over the hierarchy (the chords from the action registry). At the right, a
  slim line apart: the frame rate, the CPU time (the frame less the time the renderer waited for the GPU and the
  display, `Renderer::waitMs`, kept as the profiler's "Frame Wait") and the GPU time of the render graph's passes; each
  in a slot as wide as its widest usual value, in tabular digits, so nothing moves as they change.
- **Full viewport** ([#323]): F11, the menu bar's button left of the scene name (Lucide `maximize-2`) or View →
  Full Viewport give the viewport the whole window. The menu bar, the status bar and every panel are hidden, and the
  viewport keeps its toolbar, icons and gizmo. A small bar centered at its top holds Play, Pause and Step and the
  button that leaves (`minimize-2`), with its shortcut next to it; F11 or Esc leave too (Esc closes the drawer or a
  popup first; while the game has the input, both reach the editor only after Shift+F1, see input routing). Entering
  closes the drawer, which its chords still open over the image. The layout comes back unchanged: the dockspace and
  the docked panels are still drawn, below a window over the whole display that holds the image, since a docked
  window that isn't submitted leaves its node and an emptied node is deleted. The viewport's docked window stays
  submitted with nothing in it, and the render extent follows the image (`SettledExtent`). Floating windows
  (Preferences, the shortcuts, the style editor) are not drawn meanwhile, and F2 leaves full viewport for the
  inspector's name field. `--editor-full-viewport` starts in it (the `game-<name>-editor-full` screenshot).
- **Side rail** ([#324]): Ctrl+B, the menu bar's button left of the full viewport button (Lucide `panel-right-close`,
  `panel-right-open` while folded) or View → Fold Side Column fold the right column into a rail 32 px wide (times
  `theme::scale()`) at the window's right edge, and the viewport takes the width, as JetBrains' tool window strips and
  VS Code's Ctrl+B do. The rail holds a Hierarchy and an Inspector button (`list-tree`, `sliders-horizontal`); a click
  opens that panel in a flyout over the viewport's right edge, as wide as the column and as tall as the image, and
  lights the button. Esc (after the drawer) or the button again close it, losing the focus doesn't; a left click into
  the viewport closes the Hierarchy's flyout after it picked, while the Inspector's stays and follows the selection.
  F2 opens the Inspector's flyout for its name field. The layout comes back unchanged: while folded the dockspace is wider than the
  window by the column's width minus the rail, so the column lies off screen at its width with its panels docked in it
  and submitted empty, and ImGui gives the width to the central node. The shell begins the Hierarchy's and the
  Inspector's windows (the column panels, `Panel::column()`), so their contents go in the docked window or in the
  flyout. A column panel docked outside the right column or floating stays where it is, and its rail button focuses it.
  The layout file keeps the column folded; full viewport hides the rail and closes the flyout. `--editor-fold` starts
  folded with the Inspector in the flyout (the `game-<name>-editor-folded` screenshot).
- **Preferences** is a floating, auto-sized window that Edit → Preferences… opens, not a panel. So are Help →
  Keyboard Shortcuts (the registry's chords and the viewport's controls) and Help → About (the version and the
  fonts' licenses).
- **Open Scene** lists the project's scenes under a search field: a click selects, Enter or **Open** opens the
  selection, a double click opens at once.
- **Empty states** say what to do: the inspector with nothing selected, a hierarchy with only a new scene's
  entities, and a folder with only folders (which then lists them, a double click opens one).
- **The open scene's name** and its unsaved mark (`*`) are at the right of the menu bar, as in the window title.
- **Play mode:** the play button turns into Stop, a colored frame goes around the viewport and the windows are
  tinted, so play mode is never mistaken for edit mode. Play doesn't enter full viewport unless the "Full viewport
  on play" preference is on; Stop then leaves it, only if Play entered it and it wasn't toggled since. In full
  viewport the frame, the tint and the release hint stay, and the top bar keeps the play controls.

### Visual style

- **Theme:** dark neutral grays with one accent, a muted blue (`#4A8CD9`), for selection and the active tab.
  The play state has its own tint. Every color is a named token in `ui/theme.h` (backgrounds, text, accent,
  warning, error, play tint, axis X/Y/Z, viewport icons), never an inline value. Text reaches 4.5:1 (WCAG 2) on
  every background it is drawn on, disabled controls excepted; each token's comment gives its ratios. Filled
  controls take a darker shade of the accent (`#2A5D9C`), and read-only content (instance members, engine content)
  is muted and carries a lock icon.
- **Metrics:** frame padding 6×4, item spacing 8×4, window padding 8×8, rounding 3 on frames, popups, tabs and
  scrollbars and 0 on windows (docked windows are square), thin scrollbars. All sizes are multiplied by the
  window's content scale times the UI-scale preference, and so are the fonts; a change of either rebuilds the
  style (`theme::applyStyle`).
- **Fonts and icons** (`content/fonts/`, found through `paths::content()`, each with its license):

  | | Family | License | Use |
  |---|---|---|---|
  | UI | Inter Regular | OFL 1.1 | everything, 15 px at 100% |
  | Monospace | JetBrains Mono Regular | OFL 1.1 | the console and IDs |
  | Icons | Lucide | ISC (and MIT for icons derived from Feather) | merged into the UI font |

  ImGui 1.92 rasterizes fonts at any size, so one file per family covers every scale. Inter has alternate
  glyphs in the private-use area where Lucide's icons live: Inter excludes that range, so Lucide's win. The
  codepoints are generated, never typed: `tools/update-lucide.py` reads the font's glyph names into the partition
  `:lucide` (`ui/lucide.cppm`: every icon, `lucide::mousePointer2`), and `:theme` names the ones the editor uses
  (`icons::select = lucide::mousePointer2`). `content/fonts/Lucide-VERSION.txt` records the release and the font's
  hash; `tools/update-lucide.py <version>` updates the font, its license and the module in one step.
- **Inspector and hierarchy polish:** the inspector is a two-column grid, vector fields carry thin axis strips,
  and component headers carry icons. Each entity kind (`entityKind`: camera, directional, point and ambient light,
  sky, mesh, scene instance, scripted, group, empty) has an icon and a color (`entityIcon`, `entityIconColor`),
  shown in its hierarchy row and the inspector's header. Bold non-default values are a follow-up.
- **Shortcuts:** one action registry (`ui/actions.cppm`: id, label, chord, `enabled()`, `run()`) feeds the
  menus, the tooltips and the shortcut handler, which skips a frame while a text box has the keyboard.

  | Action | Shortcut |
  |---|---|
  | New, Open, Save, Save As | Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Shift+S |
  | Undo, Redo | Ctrl+Z, Ctrl+Y (also Ctrl+Shift+Z) |
  | Duplicate, Delete, Rename (the inspector's name field) | Ctrl+D, Del, F2 |
  | Play/Stop, Pause/Resume, Step | Ctrl+P, Ctrl+Shift+P, Ctrl+Alt+P |
  | Open or close the Assets, Console drawer | Ctrl+Space, Ctrl+Shift+Space |
  | Close the drawer | Esc |
  | Fold or unfold the side column | Ctrl+B |
  | Close the side rail's flyout | Esc |
  | Full viewport, in and out | F11 (Esc also leaves) |
  | Release the game's input (while playing) | Shift+F1 |
  | Select, Move, Rotate, Scale (viewport focus) | Q, W, E, R |
  | Focus the selection (also a double click on a hierarchy row) | F |
  | Fly the camera (while the right mouse button is held) | W, A, S, D, Q, E |

  **Collisions:** W, E and Q fly the camera while the right mouse button is held and switch the gizmo
  otherwise; the gizmo keys only act while the viewport is focused and not flying. The play chords differ
  only by their modifiers (Ctrl, Ctrl+Shift, Ctrl+Alt), so a chord matches exactly its modifiers. The
  registry asserts that no two actions share a chord.

### Trials

The owner runs the workflows (place a model, light it, edit a script field, play and stop, save and reopen, undo
a gizmo drag) on Sponza, Helmets and Simple at 1080p, 1440p and 4K, at 100%, 150% and 200%, and the tokens
are tuned from what they show. The editor's reference image comes with [#225], with the final theme.

## Runtime prerequisites

Runtime features the editor needs, in the order the editor steps need them. "Lands in" is the issue
that carries the item. Already merged: the built-in types registered
with a `ComponentStorage` each, script fields through `ScriptEntry::object`, world capture and
instantiation (`saveScene` / `instantiate`, [#131], [#183]), the `FrameScene` ([#181]) and the
`InstanceStore` ([#86]).

| # | Runtime feature | Why the editor needs it | Lands in | Needed by |
|---|---|---|---|---|
| R1 | **Component storage, complete:** `remove(registry, entity)` and a change hook `changed(registry, entity)` on `world::ComponentStorage`. Transform → `markStaticDirty` unless dynamic, MeshRenderer → `invalidateStatic` | Remove Component by name; generic edits keep D2's rule | [#191] (with R2) | E3 |
| R2 | **Type and field metadata** in `reflect`: display name, category, `required`; per field a widget hint (`Color`, `Angle`, `Slider`), min/max/step, a unit (`"m"`; angles and rotations show degrees), `readOnly`, tooltip; the asset type of `AssetRef` fields. The serializer ignores it | Generated widgets; replaces `ComponentTraits` | [#191] (with R1) | E3 |
| R3 | **UI at the safe point:** `Presentation` owns the ImGui context and both backends, builds the UI before the world update and calls `ImGui::Render` before rendering; `UiPass` only records. Headless UI with no platform backend | Edits outside the frame; headless editor screenshots | [#83] | E1 |
| R4 | **`Shell`:** startup, frame safe point, update mode, input capture (views with R5, close veto with E5); `GameShell` default; replaces `Engine::pushLayer` and the layer stack | The editor plugs in without runtime hooks of its own | [#83] (merged, layers removed too) | E1 |
| R5 | **Render views:** the shell supplies the camera (view, projection, position) instead of `extractFrameScene`'s main camera, and the scene renders into a graph texture of the view's own extent instead of the swapchain | Viewport texture, editor camera | [#192] | E2 |
| R6 | **UI textures:** `Presentation::uiTexture(ImageViewHandle) -> std::uint64_t` (an `ImTextureID`) through `ImGui_ImplVulkan_AddTexture`, released after the frames in flight and re-registered on resize | Viewport image, later thumbnails | [#193] | E2 |
| R7 | **Several worlds:** the engine creates and destroys worlds, a world-setup hook adds the engine's and the game's systems (today `Engine::resetWorld`), a clock and an `InstanceStore` with its `InstanceBridge` per world, and choosing the rendered world | Play-in-editor | [#194] | E5 |
| R8 | **Edit-mode update:** `World::update(dt, UpdateMode)`; systems opt in to `Edit` (TransformSystem, CameraSystem) | Moving things without running the game | [#83] | E1 |
| R9 | **Subtree snapshots:** `saveScene` of one entity and its descendants, and `instantiate` that keeps the IDs (undo) or gives new ones (duplicate, later paste) | Structural undo, Duplicate | [#195] | E4 |
| R10 | **Asset database for the browser:** the asset type of every entry (from the importer and the sub-asset key prefix), adding a file the editor wrote, rescan | Typed browser, picker filter, Save As | [#196] | E5, E6 |
| R11 | **Hierarchy API:** insert a child at an index (sibling order is saved, D8); misuse returns an error instead of asserting (`assert` in `registry.cppm`) | Reorder by drag and drop; undo of Reparent | [#197] | E4 |
| R12 | **Window:** content scale and its change event, title, close request that the shell can veto | High DPI, dirty title, unsaved prompt | [#198] | E5, E8 |
| R13 | **Log sinks:** add a sink without replacing stderr (today `log::setSink` replaces it); validation messages go through `aegis::log` | Console | [#199] | E7 |
| R14 | **Project and launcher:** project file (startup scene) read/write, `--project`, `--scene`, runtime data lookup | Open the project; New/Save update it | [#132] (merged) | E1 |

## Implementation plan

The editor steps, in order, each one PR (or a short stack). Each issue holds the step's content, out of
scope, "done when" and verification; this table only orders them and names the R-items they need.
[#83] (E1), the launcher ([#132]) and the legacy-core removal stay in M9, the split; the rest is in the
[M10 milestone][m10].

| Step | Issue | Needs | What it gives |
|---|---|---|---|
| E1 | [#83] (merged) | R3, R4, R8 | The shell, the editor as two libraries, today's panels; the actions, theme, fonts and Reset Layout came with E8 |
| E2 | [#200] | R5, R6 | Viewport, editor camera, gizmo in the viewport, picking |
| E3 | [#133] | R1, R2 | Inspector and Add menus generated from the type registry |
| E4 | [#165] (fields), [#201] (structure) | R9, R11 | Undo and redo of every edit |
| E5 | [#166] | R7, R10, R12 | Scene files, play-in-editor |
| E6 | [#202] | R10 | Asset browser |
| E7 | [#203] | R13 | Console, status bar, preferences |
| E8 | [#204] | R12 | The UI design: theme, fonts, layout, actions, tuned on real scenes. **Required, not optional** |
| E9 | [#205] | | The headless editor smoke test, extended by each step |

**Then:** [#172] (the legacy core goes) and [#85] (`aegis.base` becomes `aegis.core`), as planned in M9.
After M10: the nice-to-have list, starting with [#141], [#142] and [#143], which build on E3–E6.

### Parallel work

Every E-item carries `area/editor`, so they run one at a time (AGENTS.md: never two items with the
same area in parallel). The order is E1 → E2 → E3 → E4 → E5, with E6 after E2 and E7 and E9 after E1.
What runs in parallel is the prerequisite work outside the editor, while an editor item is in
progress: R5 and R6 (render lane), R10 (assets lane), and R1 + R2, R9, R11, R12 and R13 (small items
in world, platform and base). That way each E-item finds its prerequisites merged.

## Open decisions

Decided in [#83]: the name `Shell` (in `aegis.engine`, next to `Engine`), and edit mode as a public opt-in
(`System::runsInEdit`), which the engine's transform and camera systems use and a game's systems may.

- **Play world view:** replace the viewport (proposed for M10) or a separate Game panel (once two views render in one frame).
- **Editor-only game code:** how a game ships custom drawers. Proposed: an optional `editor/` source
  list in `aegis_add_game` that only `<Game>-Editor` compiles, with no macro in the runtime.

[#80]: https://github.com/chFleschutz/aegis-engine/issues/80
[#81]: https://github.com/chFleschutz/aegis-engine/issues/81
[#83]: https://github.com/chFleschutz/aegis-engine/issues/83
[#85]: https://github.com/chFleschutz/aegis-engine/issues/85
[#86]: https://github.com/chFleschutz/aegis-engine/issues/86
[#129]: https://github.com/chFleschutz/aegis-engine/issues/129
[#131]: https://github.com/chFleschutz/aegis-engine/issues/131
[#132]: https://github.com/chFleschutz/aegis-engine/issues/132
[#133]: https://github.com/chFleschutz/aegis-engine/issues/133
[#134]: https://github.com/chFleschutz/aegis-engine/issues/134
[#140]: https://github.com/chFleschutz/aegis-engine/issues/140
[#141]: https://github.com/chFleschutz/aegis-engine/issues/141
[#142]: https://github.com/chFleschutz/aegis-engine/issues/142
[#143]: https://github.com/chFleschutz/aegis-engine/issues/143
[#165]: https://github.com/chFleschutz/aegis-engine/issues/165
[#166]: https://github.com/chFleschutz/aegis-engine/issues/166
[#167]: https://github.com/chFleschutz/aegis-engine/issues/167
[#172]: https://github.com/chFleschutz/aegis-engine/issues/172
[#181]: https://github.com/chFleschutz/aegis-engine/issues/181
[#182]: https://github.com/chFleschutz/aegis-engine/issues/182
[#183]: https://github.com/chFleschutz/aegis-engine/issues/183
[#191]: https://github.com/chFleschutz/aegis-engine/issues/191
[#192]: https://github.com/chFleschutz/aegis-engine/issues/192
[#193]: https://github.com/chFleschutz/aegis-engine/issues/193
[#194]: https://github.com/chFleschutz/aegis-engine/issues/194
[#195]: https://github.com/chFleschutz/aegis-engine/issues/195
[#196]: https://github.com/chFleschutz/aegis-engine/issues/196
[#197]: https://github.com/chFleschutz/aegis-engine/issues/197
[#198]: https://github.com/chFleschutz/aegis-engine/issues/198
[#199]: https://github.com/chFleschutz/aegis-engine/issues/199
[#200]: https://github.com/chFleschutz/aegis-engine/issues/200
[#201]: https://github.com/chFleschutz/aegis-engine/issues/201
[#202]: https://github.com/chFleschutz/aegis-engine/issues/202
[#203]: https://github.com/chFleschutz/aegis-engine/issues/203
[#204]: https://github.com/chFleschutz/aegis-engine/issues/204
[#205]: https://github.com/chFleschutz/aegis-engine/issues/205
[#225]: https://github.com/chFleschutz/aegis-engine/issues/225
[#275]: https://github.com/chFleschutz/aegis-engine/issues/275
[#279]: https://github.com/chFleschutz/aegis-engine/issues/279
[#281]: https://github.com/chFleschutz/aegis-engine/issues/281
[#307]: https://github.com/chFleschutz/aegis-engine/issues/307
[#321]: https://github.com/chFleschutz/aegis-engine/issues/321
[#322]: https://github.com/chFleschutz/aegis-engine/issues/322
[#323]: https://github.com/chFleschutz/aegis-engine/issues/323
[#324]: https://github.com/chFleschutz/aegis-engine/issues/324
[#325]: https://github.com/chFleschutz/aegis-engine/issues/325
[m10]: https://github.com/chFleschutz/aegis-engine/milestone/11
