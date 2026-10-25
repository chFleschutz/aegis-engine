# Plan: refine the editor UI design (#204, E8)

The implementation plan for [#204], decided with the owner. The design it refines is `docs/editor.md`
("UI design: initial proposal"); once this lands, that section records the decisions and this plan can go.

## Decisions

| Topic | Decision |
|---|---|
| UI font | Inter (OFL 1.1) |
| Monospace font | JetBrains Mono (OFL 1.1), for the console and IDs |
| Icons | Lucide (ISC), merged into the UI font |
| Font home | `content/fonts/` (engine content, found through `paths::content()`), licenses next to the files |
| Accent | Muted blue `#4A8CD9`: selection, active tabs. The play tint stays its own token |
| Scope | Theme, fonts, scaling, the dockspace layout and the viewport toolbar with icons |
| Shortcuts | A small action registry in #204 |
| Reference image | Stack #204 on [#205]; the editor's golden reference comes with [#225] |
| Trials | The owner runs the workflow and scale trials; the tokens are tuned from the findings |

## State before the work

- **No theme module.** `applyStyle()` in `editor_shell.cpp` sets a few metrics over `StyleColorsDark`; about
  14 colors are inline (play tint, status bar, Save As error, console levels, viewport icons).
- **No dockspace.** Panels float; the only layout is a 400×600 "SceneLayer" node in `scene_panel.cpp`. The
  hierarchy window is misspelled "Hierachy".
- **No fonts loaded** (ProggyClean). Scaling is only `FontScaleMain = uiScale`: neither the window's content
  scale (R12) nor the metrics.
- **Shortcuts are hard-coded** in `EditorShell::handleShortcuts` and the viewport; the menus repeat their
  labels. Del and F2 are missing.
- ImGui is 1.92.4: fonts rasterize at any size, so one TTF per family covers every scale.

## Order

1. **[#205] first** (`test/editor-smoke`, size S): `<Game>-Editor --headless --screenshot` as a `device`
   test, with an editor option that opens every panel. #204 stacks on it, so the before and after
   screenshots come from the same command.
2. **#204** on `feat/editor-ui-design`, in reviewable commits:
   1. **Rename** "Hierachy" to "Hierarchy" (mechanical; resets old ini layouts, which the new layout does anyway).
   2. **Fonts:** Inter, JetBrains Mono and Lucide (regular weights) in `content/fonts/` with their `OFL.txt` /
      ISC `LICENSE`, in a commit of their own.
   3. **`ui/theme`** (a `:theme` partition of `aegis.editor`): named color tokens (background shades, accent,
      warning, error, play tint, axis X/Y/Z), metric tokens (frame padding 6×4, item spacing 8×4, window
      padding 8×8, rounding 3 on frames and popups, 0 on docked windows, thin scrollbars), font loading (Lucide
      merged into Inter, JetBrains Mono for the console and IDs) and a small header of the Lucide codepoints the
      editor uses. It replaces `applyStyle` and every inline color. Headless runs load the fonts too, so
      screenshots match a windowed run.
   4. **Scaling:** content scale × UI-scale preference drives `FontScaleDpi` / `FontScaleMain`, and the style
      is rebuilt from the tokens with `ScaleAllSizes` when either changes. Check whether the shell sees the
      window's content scale already; else add an `Engine` accessor.
   5. **Layout:** a dockspace in the main window and the `DockBuilder` split of `docs/editor.md` (hierarchy
      about 18%, inspector about 22%, assets and console tabbed at the bottom about 25%, the viewport central),
      built on first start and by **View → Reset Layout**. All Entities and Scene Settings become tabs beside
      the hierarchy; Renderer, Profiler and Statistics start closed and open as tabs beside the inspector. A
      **Debug** menu holds the Style Editor and the Demo window.
   6. **Action registry:** `{id, label, chord, enabled, run}` in the UI layer, read by the menus and the
      shortcut handler; adds Del and F2. Viewport-only keys (Q/W/E/R, F) keep their focus rule.
   7. **Toolbar and play state:** Lucide icons for the gizmo modes and play/pause/step, a colored frame around
      the viewport while playing, the menu-bar tint from the tokens.
   8. **Docs:** "UI design: initial proposal" in `docs/editor.md` becomes the decisions (fonts, icons and
      their licenses, the shortcut table and its collisions: WASDQE flight vs the Q/W/E/R gizmo keys, Ctrl+P vs
      Ctrl+Shift+P vs Ctrl+Alt+P); the "Fonts and icons" open decision closes.
3. **Trials:** the owner tries the workflows (place a model, light it, edit a script field, play and stop,
   save and reopen, undo a gizmo drag) on Sponza, Helmets and Simple at 1080p, 1440p and 4K, at 100%, 150% and
   200%. The tokens are tuned in follow-up commits; the PR carries the before and after headless screenshots.

## Out of scope

- The editor's reference image: [#225], with the final theme.
- Inspector and hierarchy polish (a follow-up issue): colored X/Y/Z labels, component header icons and menus,
  an icon per entity kind in the hierarchy, bold non-default values.

## Risks

- **Size:** with the registry and the layout this is a large M. If the review gets heavy, the action registry
  (step 6) becomes its own PR.
- **Vulkan backend:** 1.92's dynamic font atlas needs the backend's texture updates (`RendererHasTextures`).
  Check that the UI pass handles them before relying on runtime font sizes.

[#204]: https://github.com/chFleschutz/aegis-engine/issues/204
[#205]: https://github.com/chFleschutz/aegis-engine/issues/205
[#225]: https://github.com/chFleschutz/aegis-engine/issues/225
