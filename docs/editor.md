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

- **Done:** E1 ([#83]: R3, R4, R8; the action registry, theme v0, fonts v0 and Reset Layout moved to
  [#223]), E2 ([#200]), E3 ([#133]), the field half of E4 ([#165]: `undo_stack`, Ctrl+Z / Ctrl+Y, the Edit menu), R1 and R2 ([#191]), R10 ([#196]), R12 ([#198]), R13 ([#199]), R14 ([#132]).
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
│  ├─ property_model   TypeInfo/FieldInfo + metadata -> rows {label, widget, limits, readOnly}
│  ├─ inspector        the components and scripts an entity shows and can add; field writes + change hook
│  ├─ hierarchy_model  entity tree as rows, filtered by name; pure
│  ├─ play_session     Edit -> Playing <-> Paused -> Edit, step
│  ├─ editor_camera    fly/orbit/pan/dolly/focus math -> view, projection and picking rays
│  ├─ picking          ray against bounding spheres, icon hits in screen space
│  └─ settled_extent   the viewport's render size, applied once the panel's size settles
└─ ui/                            aegis-editor, module aegis.editor (ImGui in .cpp only)
   ├─ editor_shell     implements Shell; owns the context and the panels
   ├─ actions          action registry: id, label, shortcut, enabled(), run()
   ├─ theme            colors, sizes and fonts in one place
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
- **Controls:** RMB + WASD/QE to fly (wheel changes the speed), Alt + LMB to orbit, MMB to pan, wheel to
  dolly, F to focus the selection (its bounding sphere). Z is up, matching D8.
- **Gizmos:** ImGuizmo, drawn into the viewport window's draw list and clipped to the image rect. It has
  translate, rotate and scale, a local/world toggle, and per-mode snap values from the editor
  preferences (Ctrl inverts snapping). W/E/R/Q work only while the viewport is focused. One drag is one
  undo command: the transform is captured when the drag starts and committed when it ends.
- **Picking:** at first a CPU ray against each `MeshRenderer`'s world-space bounding sphere
  (`StaticMesh::bounds()`), nearest hit, with Alt-click cycling through overlapping hits. Lights and
  cameras get screen-space icons drawn with the viewport draw list, and those are clickable too. A GPU
  ID buffer is a later improvement (nice to have).
- **Input routing:** the editor camera gets input while the viewport is hovered or RMB is held. The
  play world gets game input only while the viewport is focused in play mode. Shift+F1 releases a
  captured cursor. The shell sets the capture flags: ImGui's, plus everything while the viewport is
  hovered or the camera is being dragged.

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
  commit on deactivate); AssetRef → an asset slot that shows the asset's name, opens a picker filtered
  by the field's asset type, accepts drops from the browser, and offers "None". The mapping is a pure
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
  through `ScriptEntry::object`. "Add Script" lists the registry's script types. Attach and detach are
  commands.
- **Instance members are read-only** until overrides exist ([#142]). Entities created by a scene
  instance carry `InstanceMember`, and `saveScene` leaves them out, so an edit to them would be lost
  without warning. The inspector shows them disabled, with a note naming the instanced scene, and the
  hierarchy shows instances with their own icon. The holder (with its `SceneInstance` and
  `Transform`) stays editable.
- **Custom drawers are the exception.** The generic drawer plus metadata covers every built-in type.
  A drawer registry keyed by type name (editor-side) exists for the few that need more (a future
  `SkinnedMesh`, curves). A game registers drawers from its editor-only sources. Nothing in the
  runtime knows about them.
- **Reset and copy:** the component context menu offers Reset (values from a default instance,
  `TypeInfo::create`), Copy and Paste (field values as text through `toText` / `fromText`) and Remove.
  All of these are commands.
- **Structural edits are commands too:** Create, Destroy and Duplicate store the entity's subtree as a
  `serialize::Scene` snapshot (R9), so undo restores it with its IDs and Duplicate gives the copy new
  ones.
  Add and Remove component store the component's field values. Reparent stores the old parent and the
  sibling index (R11).
- **Undo stack:** commands with `apply` / `revert` on a `World`, merged by key (same entity, type and
  field, within one interaction), with a saved marker. The dirty flag is "position != saved". It is
  pure and tested on a device-less world ([#165]).
- **Hierarchy:** rows come from `Parent` / `Children` / `Siblings`, walked iteratively (as today),
  with a name filter (matching rows keep their ancestors). Drag and drop reparents and reorders,
  rejecting cycles (`Registry::isInSubtree`). F2 renames, Del deletes, Ctrl+D duplicates, and the
  context menu has the Create submenu (Empty, Camera, Point Light, Directional Light, and the
  registry's types).
- **Asset browser (R10):** a folder tree over `AssetDatabase::entries()` with two roots, the project
  and the engine content (read-only, shown dimmed). The file list shows each file's sub-assets
  expandable, with search and a type filter. A drag carries an `AssetId` payload with its type:
  dropping it on an asset slot assigns it (type-checked), and dropping a `Scene` or model on the
  viewport or hierarchy instantiates it (`AssetLibrary::instantiateScene` with a parent; models
  become scenes in [#140]).
  Double-clicking a `.scene` opens it. A Rescan button covers the time before the file watcher
  ([#143]).
- **Scene files:** New, Open (a picker over the database's `Scene` entries), Save (Ctrl+S), and Save As
  (an in-editor path field inside the project, which writes a new native file with a new ID and adds
  it to the database). The title bar and the scene tab show `*` while dirty. Open, New and closing the
  window ask about unsaved changes (`Shell::canClose`, R12). There are no native file dialogs.
- **Console:** a log sink (R13) feeds a ring buffer with level filters, search, clear and copy, while
  stderr output continues. The status bar shows the last warning or error.

### Persistence

- **Layout:** `imgui.ini` goes in `<project>/.aegis/editor/imgui.ini` (per project, per machine, never
  committed, D8's one ignored folder). A View → Reset Layout action rebuilds the default with
  `DockBuilder`.
- **Preferences** (camera speed, snap values, recent projects, UI scale) go in the user config folder
  (`%APPDATA%/Aegis/editor.json`, `$XDG_CONFIG_HOME/aegis/editor.json`), as JSON through glaze in a
  `.cpp`.
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
   and Add Component and Add Script menus ([#133]).
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
15. **A headless editor smoke test** (`<Game>-Editor --headless --frames 60 --screenshot`) as a
    `device` CTest.

### Nice to have (after M10, mostly Backlog)

- Multi-selection with multi-edit (fields that differ shown as mixed) and a shared pivot for gizmos.
- Copy and paste of entities across scenes, with the clipboard holding a scene fragment as JSON.
- Selection outline and GPU ID-buffer picking (exact, works for the GPU-driven path).
- Grid, view cube (`ImGuizmo::ViewManipulate`), and debug drawing (light ranges, camera frusta,
  bounds). This needs a debug-line pass in the render graph.
- Render debug views (G-buffer channels, wireframe, overdraw) and a stats overlay in the viewport.
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

## UI design: initial proposal

This is a starting point to build against. **E8 is a required task to refine it** once the editor is
usable: the layout, sizes and colors below are guesses until they have been tried on real scenes.

### Layout

```
┌────────────────────────────────────────────────────────────────────────────────────┐
│ File Edit View Entity Debug Help          ▶  ❚❚  ⏭          Examples · sponza.scene* │ menu bar + play controls
├─────────────────┬──────────────────────────────────────────────────┬───────────────┤
│ Hierarchy       │ ✥ ⟳ ⤢ │ Local ▾ │ Snap ▾ │ Camera ▾ │ Shading ▾   │ Inspector     │ viewport toolbar
│ [filter      ]  │ ┌──────────────────────────────────────────────┐ │ Helmet        │
│ ▾ Sponza        │ │                                              │ │ ▾ Transform   │
│   ▸ Helmet      │ │                                              │ │   Location …  │
│   ▸ Lights      │ │              Viewport (scene texture)        │ │   Rotation …  │
│     Camera  📷  │ │                    gizmo                     │ │   Scale    …  │
│     Sun     ☀   │ │                                              │ │ ▾ MeshRenderer│
│                 │ │                                              │ │ ▾ Scripts     │
│                 │ └──────────────────────────────────────────────┘ │ [Add …     ▾] │
├─────────────────┴────────────────────────────┬─────────────────────┴───────────────┤
│ Assets   [project ▾] [search   ] [type ▾]    │ Console   [info][warn][error] [ ]   │ bottom dock, tabbed
│ ▸ models/  ▸ scenes/   Helmet.gltf  ▸ …      │ [warn] Missing asset 1f6b… (fallback)│
├──────────────────────────────────────────────┴─────────────────────────────────────┤
│ 1 selected · 60 fps · GPU 4.1 ms · ⚠ last warning                         ● unsaved │ status bar
└────────────────────────────────────────────────────────────────────────────────────┘
```

- **Default split:** hierarchy on the left (about 18% of the width), inspector on the right (about 22%),
  and assets and console tabbed at the bottom (about 25% of the height). The viewport is the central
  node and is never undocked by default. Built with `DockBuilder` on first start and by Reset Layout.
- **Debug panels** (Renderer, Profiler, Statistics) are closed by default. They open as tabs next to
  the inspector.
- **Play mode:** the play controls change state, a colored frame goes around the viewport, and the menu
  bar is tinted, so play mode is never mistaken for edit mode.

### Visual style

- **Theme:** dark neutral grays with one accent color for selection, active tabs and the play state.
  The rest of ImGui's default blue goes. All colors and sizes are named tokens in `ui/theme`
  (accent, warning, error, play tint, axis X/Y/Z = red/green/blue), never inline `PushStyleColor`
  values.
- **Metrics:** frame padding 6×4, item spacing 8×4, window padding 8×8, rounding 3 on frames and
  popups, 0 on docked windows, thin scrollbars. Everything is scaled by the window's content scale
  (R12) times the UI-scale preference.
- **Fonts:** a proportional UI font (for example Inter) at about 15 px, a monospace font for the console
  and IDs (for example JetBrains Mono), and an icon font merged into the UI font (for example Lucide or
  Material Symbols). They ship as engine content under OFL/ISC-style licenses. The licenses are checked
  in E8, which makes the final pick.
- **Inspector:** a two-column property grid (an ImGui table with labels at about 40%, widgets filling
  the rest) instead of ImGui's labels on the right. Vectors have colored X/Y/Z labels. A component
  header holds its name, an icon and a context-menu button. Values that differ from the default are
  shown in bold (nice to have).
- **Hierarchy:** an icon per entity kind (camera, light, mesh, empty, scene instance), full-width
  selection rows, and a drop indicator line for reordering.
- **Shortcuts:** Ctrl+N/O/S, Ctrl+Shift+S, Ctrl+Z, Ctrl+Y and Ctrl+Shift+Z, Ctrl+D, Del, F2, F,
  W/E/R/Q (viewport focus), Ctrl+P play/stop, Ctrl+Shift+P pause, Shift+F1 release the cursor. All
  come from the action registry, so menus show them.

## Runtime prerequisites

Runtime features the editor needs, in the order the editor steps need them. "Lands in" is the issue
that carries the item. Already merged: the built-in types registered
with a `ComponentStorage` each, script fields through `ScriptEntry::object`, world capture and
instantiation (`saveScene` / `instantiate`, [#131], [#183]), the `FrameScene` ([#181]) and the
`InstanceStore` ([#86]).

| # | Runtime feature | Why the editor needs it | Lands in | Needed by |
|---|---|---|---|---|
| R1 | **Component storage, complete:** `remove(registry, entity)` and a change hook `changed(registry, entity)` on `world::ComponentStorage`. Transform → `markStaticDirty` unless dynamic, MeshRenderer → `invalidateStatic` | Remove Component by name; generic edits keep D2's rule | [#191] (with R2) | E3 |
| R2 | **Type and field metadata** in `reflect`: display name, category, `required`; per field a widget hint (`Color`, `Angle`, `Slider`), min/max/step, `readOnly`, tooltip; the asset type of `AssetRef` fields. The serializer ignores it | Generated widgets; replaces `ComponentTraits` | [#191] (with R1) | E3 |
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
| E1 | [#83] (merged), [#223] | R3, R4, R8 | The shell, the editor as two libraries, today's panels; actions, theme v0, fonts v0, Reset Layout in [#223] |
| E2 | [#200] | R5, R6 | Viewport, editor camera, gizmo in the viewport, picking |
| E3 | [#133] | R1, R2 | Inspector and Add menus generated from the type registry |
| E4 | [#165] (fields), [#201] (structure) | R9, R11 | Undo and redo of every edit |
| E5 | [#166] | R7, R10, R12 | Scene files, play-in-editor |
| E6 | [#202] | R10 | Asset browser |
| E7 | [#203] | R13 | Console, status bar, preferences |
| E8 | [#204] | R12 | The UI design, refined on real scenes. **Required, not optional**; starts once E1–E6 are usable |
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
- **Fonts and icons:** which families, and whether they live in engine content or `external/`
  (decided in E8).

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
[#223]: https://github.com/chFleschutz/aegis-engine/issues/223
[m10]: https://github.com/chFleschutz/aegis-engine/milestone/11
