# Editor UI review

A review of the editor's UI as of `9c31ca0f` (after [#204] merged: theme tokens, Inter/JetBrains Mono/Lucide, the
docked layout, the action registry and the toolbar icons). It lists what still gets in the user's way and suggests
fixes. Finding IDs (**UI-n**) are for issues to cite. Ideas already in the nice-to-have list in
[`editor.md`](editor.md) (grid, view cube, thumbnails, command palette, multi-selection, ...) aren't repeated here
unless the review changes their priority.

**How it was reviewed:** by reading `src/aegis/editor/ui/` and from headless screenshots of `Examples-Editor`
(Sponza, Helmets, Simple; 1920×1080 and 1280×720; default panels and `--editor-panels all`):

    Examples-Editor --headless --frames 60 --width 1920 --height 1080 --scene scenes/sponza.scene --screenshot sponza.png

Interaction (selection, the inspector with an entity, play mode, drag and drop) couldn't be screenshotted headless,
so those findings come from the code. UI-02 in particular should be confirmed by hand before fixing.

## Summary

The foundation from [#204] is good: the theme is calm and consistent, the fonts and icons read well, menus show
their shortcuts, and the viewport toolbar is clear. The main problems are elsewhere:

1. **Two bugs:** a fresh editor opens with no Hierarchy and no Inspector (UI-01), and the hierarchy shows children
   in reverse order, which inverts the reorder drop zones (UI-02).
2. **The Hierarchy is the weakest panel:** no name filter, a one-item Create menu, no row context menu, no icons
   (UI-10 to UI-14).
3. **Docking chrome and empty states waste space or confuse:** the Viewport can be closed, every dock node has a
   menu and a close button, the Inspector is blank with nothing selected, the Assets root shows "No assets"
   (UI-04 to UI-07).
4. **High DPI isn't finished:** icons, dialogs and some widths are raw pixels and don't scale (UI-30).
5. **The smoke-test screenshot hides the editor** behind the ImGui demo window (UI-33).

## Bugs

### UI-01: A fresh editor has no Hierarchy and no Inspector
Only Assets and Console start open: `AssetPanel` and `ConsolePanel` call `setOpen(true)` in their constructors,
`ScenePanel` and `InspectorPanel` don't, and `m_defaultOpen` (`editor_shell.cpp`, the `EditorShell` constructor)
records the panels open at construction, so **View → Reset Layout doesn't bring them back either**. The default
screenshot shows the viewport over the whole width. The smoke test misses it because it passes `--editor-panels all`.

**Fix:** open `ScenePanel` and `InspectorPanel` by default. Make the smoke test (or a second one) run without
`--editor-panels all`, so the default layout is what gets checked (UI-33).

### UI-02: The hierarchy shows children in reverse sibling order (verify by hand)
`drawHierarchy` (`scene_panel.cpp`) walks the tree with a stack: it pushes the children first to last and pops the
last one first, so a parent's children appear last-to-first. `Registry::insertChild` keeps them first-to-last, and
the drop zones (`dragAndDrop`) assume the screen order is the list order. Dropping on the top quarter of a row
("before") puts the entity before it in the list, which then *shows it below the row*. The scene file and the
hierarchy also disagree about the order. Roots are unaffected (they come out in file order).

**Fix:** push the children in reverse (walk `Siblings::prev` from `children.last`). Better: build the rows in
`aegis-editor-core` as the planned `hierarchy_model` (rows with depth, filtered by name), unit-test the order there,
and keep the panel a thin loop over rows. That also gives UI-10 its filter.

### UI-03: Small inconsistencies
- The asset-slot picker's search is case-sensitive (`name.find(s_filter)`, `inspector_panel.cpp`); the asset
  browser and the console ignore case. Reuse the browser's `containsIgnoringCase`.
- `EditorPreferences::snapTranslate`'s comment says "while Ctrl is held"; snapping is now a toolbar toggle that Ctrl
  inverts.
- The Statistics panel is "Statistics" in the menu and "Scene Statistics" as a window; the Renderer, Profiler and
  Statistics windows have no close button on their tab (`Begin` without `p_open`), unlike Assets and Console.

## Layout and docking chrome

### UI-04: The Viewport can be closed and carries a tab
The viewport is a normal docked window with a tab and a close button, and the empty central node shows nothing
useful when it's closed. Give the central node `ImGuiDockNodeFlags_AutoHideTabBar` (or hide the tab bar of the
viewport's node) and drop its close button; "Viewport" in the View menu then only matters after an undock. This
gains a row of height and removes an easy mistake.

### UI-05: Every dock node shows a window-menu arrow and a close-all button
Each node has the ▼ menu at its left and an × at its right (visible in every screenshot), in addition to the tab's
own ×. Pass `ImGuiDockNodeFlags_NoWindowMenuButton | ImGuiDockNodeFlags_NoCloseButton` to `ImGui::DockSpace`. The
View menu already lists the panels.

### UI-06: The right dock overflows into truncated tabs
With all panels open, the right node has five tabs at 22% width: "Inspector", "Preferen…", "Renderer",
"Scene St…", "Pr◀▶". Suggestions:
- **Preferences is a settings dialog, not a dock tab:** move it to **Edit → Preferences…** (or File) as a floating
  window or modal, as most tools do.
- **Debug panels (Renderer, Profiler, Statistics) belong at the bottom,** tabbed with Assets and Console: they are
  wide tables, and the bottom node is wide. The Debug menu (only the ImGui demo and the style editor today) should
  list them, as `editor.md` planned (`Panel::debug()` returns false for them).

### UI-07: Empty states say nothing useful
- **Inspector with nothing selected** is blank. Show a centered, dimmed "Select an entity in the Hierarchy or the
  viewport".
- **Assets with a root folder selected** says "No assets in this folder" although it holds four folders. Show the
  folder's subfolders as rows in the file list (double-click to enter), or select the first folder with files.
- **Hierarchy of an empty scene** (Template): a hint "Right-click to create an entity, or drop a model here".

### UI-08: The title of the open scene isn't visible in the editor
The window title has it (`sponza.scene* - Aegis Editor`), but in a maximized or full-screen window, and in
screenshots, nothing in the UI names the scene. The mock-up in `editor.md` put "Examples · sponza.scene*" at the
right of the menu bar; that slot is still empty and would replace the status bar's "* unsaved".

## Viewport

### UI-09: Light and camera icons clutter the view
Simple's 64 point lights draw 64 full-size icons that hide the floor; every icon is the same 9 px circle with a
yellow outline regardless of distance or light color.
- A **"Gizmos" toggle** in the viewport toolbar to hide the icons (and, later, grid and debug drawing).
- **Tint the icon with the light's color** and fade or shrink it with distance; draw the selected one last, on top.
- Scale `iconRadius` with the UI scale (UI-30).

Also worth adding to the toolbar, at its right: the **camera speed** (shown while flying, editable on hover — today
it only changes with the wheel, invisibly) and a menu for the stats overlay that's in the nice-to-have list.

## Hierarchy

### UI-10: No name filter
`editor.md` lists the filter ("matching rows keep their ancestors") as a must-have; the panel has none. With Sponza
or the stress scenes the list is long. Add a search field at the top (it comes with the row model of UI-02).

### UI-11: Create is one item, and only in a window context menu
The context menu offers "Create Entity", "Duplicate Selected Entity" and "Destroy Selected Entity". The design asks
for a Create submenu (Empty, Camera, Point Light, Directional Light, and the registry's types).
- A **"+" button** next to the filter that opens the Create menu; the same menu as the context menu's **Create ▸**
  and a top-level **Entity** menu in the menu bar.
- Create the new entity **under the selected one** when made from its row's menu, and select and rename it (F2
  focus) right away.

### UI-12: Right-click on a row acts on the old selection
The context menu is the window's (`BeginPopupContextWindow`), so right-clicking an unselected row then "Destroy
Selected Entity" destroys whatever was selected before. Give each row its own context menu
(`BeginPopupContextItem`) that selects the row first, with Rename, Duplicate, Delete, Create Child ▸ and Focus.
Name the items after the actions (the registry's labels and shortcuts) instead of "… Selected Entity".

### UI-13: Rows have no icons, and instances look like plain entities
All rows look the same. Already planned as the "inspector and hierarchy polish" follow-up of [#204], which has no
issue yet: an icon per kind (mesh, camera, light, empty, scene instance), and **instance members dimmed** like the
read-only engine content in Assets, since the Inspector won't let you edit them anyway.

### UI-14: "All Entities" and "Scene Settings" are developer views
"All Entities" is a flat dump of every entity (the hierarchy flattened); "Scene Settings" lists the main camera,
ambient light and directional light, which the hierarchy already shows. Move All Entities to the Debug menu. Fold
Scene Settings into the Hierarchy as pinned rows or into a future Project/Scene settings panel (nice-to-have list).

Two smaller ones: double-clicking a row opens the tree node instead of focusing the camera (most editors frame the
entity; with `OpenOnArrow` the arrow still opens it), and there's no drop indicator into a row other than the
default rectangle.

## Inspector

### UI-15: Component headers have no context menu
The header's only action is its × (remove). The design asks for **Reset, Copy, Paste, Remove** in a context menu,
and they map onto existing commands (`setField` per field, `defaultValues`). Add a ⋮ button at the right of the
header plus right-click on it. Removing a component with a stray click on × is easy at small sizes: the menu makes
removal deliberate.

### UI-16: Vectors have no axis labels
`DragScalarN` shows three unlabeled boxes; you can't tell X from Z at a glance, and the axis tokens
(`theme::axisX/Y/Z`) exist but aren't used here. Draw a colored X/Y/Z strip or letter before each component (planned
in the [#204] follow-up), and let a click on the letter reset that component.

### UI-17: Colors edit as floats without a hint
`drawColor` uses `ImGuiColorEditFlags_Float`: the user sees `0.800 0.800 0.800` with no swatch-first layout and no
sign whether that's linear or sRGB. `editor.md` says colors are linear and edited in sRGB. Show the swatch and hex
by default (`ImGuiColorEditFlags_DisplayHex` or the picker on click) and convert in the widget, or label the field
"linear" in its tooltip.

### UI-18: The header spends a row on the ID
The name field, then a line "ID 1f6b… [Copy]", then a separator, before the first component. The ID matters
rarely; move it into the name row's tooltip or a ⋮ menu ("Copy ID"), and put **component toggles** (enable/disable,
once the runtime has them) and the entity's DynamicTag state in the header instead.

### UI-19: Add Component and Add Script are two buttons for one action
Two half-width buttons at the bottom, each opening a menu with no search. Merge them into one full-width "Add…"
button whose popup has a search field and both lists (components, then scripts), as Unity does. With many script
types the search is what makes it usable.

## Assets

### UI-20: The browser shows one column of names
- Files in the list show no type icon; the type is only in a tooltip and the filter combo. A small type icon (mesh,
  material, texture, scene, environment) per row helps before thumbnails exist.
- The folder column takes 30% of a full-width panel; at 1920 px that's ~575 px for four folder names. Start it at a
  fixed ~200 px (scaled) and let the files take the rest.
- Double-click is the only way to open a scene; add a row context menu (Open, Instantiate into scene, Copy ID,
  Show in Explorer later).

## Console and status bar

### UI-21: The status bar keeps a stale warning forever
After loading Sponza the status bar shows "warn: StaticMesh 0 has no name; …" until another warning comes, with no
way to clear or act on it. Suggestions:
- **Counts instead of the message:** "⚠ 25  ⛔ 0" at the right, with the last message as the tooltip.
- **Click opens the Console** (focus its tab) with that level filtered.
- Clear the counts when the Console is cleared (it clears `lastProblem` already).

### UI-22: The status bar's selection count says little
"0 selected" / "1 selected" — show the active entity's name instead (and the count once multi-selection exists).
The FPS and GPU time are useful but jump every frame; show the rolling average the profiler already keeps.

### UI-23: Console polish
- The Info/Warn/Error checkboxes could show their counts ("Warn 25") and be toggle buttons, which take less room.
- Repeated messages (the 25 "Material n has no name" warnings) could collapse into one line with a count.
- `m_search` is a fixed 256-byte buffer while every other field uses `std::string` (`imgui_stdlib`).

## Menus, dialogs and preferences

### UI-24: Menus worth adding
The design's menu bar was File, Edit, View, Entity, Debug, Help; today it's File, Edit, Play, View, Debug. The
deviations from the plan are fine (the Play menu stays); these are the additions that help:
- **Create** in the menu bar (named Entity or Create): the Create menu of UI-11, plus Focus (F).
- **Help**: the shortcut table (generated from the action registry) and an About with the font licenses.
- **File → Open Recent**: the preferences keep `recentProjects`, but nothing uses them; recent *scenes* of the project
  would help more within one project.
- **Edit → Undo** could name the edit ("Undo Move Helmet"): the undo stack's `describe(edit)` already exists.

### UI-25: Open Scene needs a double-click
The dialog lists scenes, and only a double-click opens one; selecting a row and pressing Enter or an **Open**
button does nothing. Add an Open button (default) and Enter, and a search field once projects have many scenes.

### UI-26: The UI-scale drag rescales the UI under the cursor
Dragging "UI scale" in Preferences rebuilds the style every frame, so the widget moves while it's dragged. Use a
combo of steps (75%, 100%, 125%, 150%, 175%, 200%), applied on selection. Show the effective scale (content scale ×
preference) next to it.

### UI-27: Recent projects are plain text
The Preferences panel lists them as unclickable text. Either make them open the project (relaunch with `--project`)
or leave them out until a welcome screen exists.

## Play mode

### UI-28: Play state is easy to miss in the menu bar
The play controls are three small buttons centered in the menu bar, the same color as the menus; the frame around
the viewport and the tint are good. Add a **"Playing" / "Paused" label** next to the buttons in the play color,
and make Play a filled button while active.

### UI-29: Keyboard focus while playing
While playing with the viewport focused, `inputCapture()` gives the game all input
(`Viewport::givesInputToGame`), and the shell's shortcuts still run from ImGui's keys, so Ctrl+P stops play *and*
reaches the game as P, and Ctrl+S/Ctrl+Z also reach it. Shift+F1 (release the cursor, in the design's shortcut list)
isn't implemented. Suggestions: hold back a chord the shell handled from the game that frame, add Shift+F1, and show
"Shift+F1 releases the cursor" in the viewport while the game has input.

## High DPI and scaling

### UI-30: Raw pixel sizes don't scale
`applyStyle` scales the style and the fonts, but several sizes are fixed pixels:
- the viewport icons (`iconRadius{ 9.0f }`, also the click radius),
- the Open Scene dialog (420×360), the Save As field (360), the asset type combo (160),
- the status bar's padding (8×2), the Renderer and Profiler's first-use positions.

Multiply them by a `theme::scale()` (content scale × UI scale), or express them in `GetFontSize()` / `GetFrameHeight()`
units as the play frame already does. At 200% these widgets are half the size of everything around them.

## Review tooling

### UI-31: Headless runs can't show a selection
The inspector and the gizmo are the panels that most need a visual check, but a headless run never selects
anything. A `--editor-select <name>` option (debug only, like `--editor-panels`) would put the Inspector and the
gizmo into the screenshot and the future reference image ([#225]).

### UI-32: No screenshot of play mode
Likewise a `--editor-play` option that enters play after the first frame would capture the tint and the frame.

### UI-33: `--editor-panels all` opens the ImGui demo
The demo window floats over the viewport and covers about a third of the smoke test's screenshot; the reviewer
sees the demo, not the editor. Exclude `debug()` panels from `all`. Also run the smoke test once with the default
panels (UI-01).

## Issues

The findings are tracked in M10, one issue per group, each sized for one session:

| Issue | Findings | Size | Blocked by |
|---|---|---|---|
| [#272] Default panels, hierarchy order (with the `hierarchy_model` rows), smoke test | UI-01, UI-02, UI-33 | M | |
| [#273] Hierarchy filter, Create menu, row context menus | UI-10, UI-11, UI-12, UI-14 | M | #272 |
| [#274] Dock chrome, empty states, panel placement | UI-03 to UI-08 | M | |
| [#275] Inspector component header menus, one Add menu | UI-15, UI-18, UI-19 | M | |
| [#276] Axis-labeled vectors, sRGB colors | UI-16, UI-17 | S | |
| [#277] Entity and asset kind icons | UI-13, UI-20 | S | #273 |
| [#278] Scale fixed pixel sizes, UI-scale steps | UI-30, UI-26 | S | |
| [#279] Status bar counts and selection, console | UI-21, UI-22, UI-23 | S | |
| [#280] Editor shortcuts kept from the game, play-state label | UI-28, UI-29 | S | |
| [#281] Help, Open Recent, undo labels, Open dialog | UI-24, UI-25, UI-27 | S | |
| [#282] Viewport icon toggle and tint, camera speed | UI-09 | S | |
| [#283] Headless selection and play options | UI-31, UI-32 | S | #272 |

[#272] goes first (bugs). The rest can follow in any order, one at a time (`area/editor`).

[#204]: https://github.com/chFleschutz/aegis-engine/issues/204
[#225]: https://github.com/chFleschutz/aegis-engine/issues/225
[#272]: https://github.com/chFleschutz/aegis-engine/issues/272
[#273]: https://github.com/chFleschutz/aegis-engine/issues/273
[#274]: https://github.com/chFleschutz/aegis-engine/issues/274
[#275]: https://github.com/chFleschutz/aegis-engine/issues/275
[#276]: https://github.com/chFleschutz/aegis-engine/issues/276
[#277]: https://github.com/chFleschutz/aegis-engine/issues/277
[#278]: https://github.com/chFleschutz/aegis-engine/issues/278
[#279]: https://github.com/chFleschutz/aegis-engine/issues/279
[#280]: https://github.com/chFleschutz/aegis-engine/issues/280
[#281]: https://github.com/chFleschutz/aegis-engine/issues/281
[#282]: https://github.com/chFleschutz/aegis-engine/issues/282
[#283]: https://github.com/chFleschutz/aegis-engine/issues/283
