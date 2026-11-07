# Aegis Editor UI: Design Principles & UX Priorities

Nov 6, 2026 · @Chris

## Context and goals

The Aegis editor should feel like a conventional, fast, dockable professional tool, and the highest-value work is the cross-cutting infrastructure (undo, reflection-driven inspectors, responsiveness) rather than visual polish.

- **Stack:** Dear ImGui (docking branch) + GLFW + Vulkan, with the RHI/Renderer split on the engine side.
- **Scope:** layout conventions, UX priorities, patterns to avoid, ImGui-specific notes, and a suggested build order.
- **Audience:** a single developer reworking the editor UI, so the advice favors decisions that are expensive to retrofit.

## Layout principles

Follow the layout that Unreal, Unity, Godot and Blender have converged on, so users arrive already knowing where things are.

**Dockable panels with a good default.** Users differ in what they keep on screen, so panels must dock, tab and tear off. Ship one strong default layout, let users save and restore layouts, and offer task presets such as Level Editing, Material Authoring and Debugging.

**A conventional core arrangement.**

| Region | Panels | Notes |
| --- | --- | --- |
| Center | Viewport | Largest surface; everything else supports it |
| Left or right | Hierarchy/outliner, details/inspector | Inspector follows the current selection |
| Bottom | Asset browser, console/log | Tabbed; log entries link back to objects |
| Top | Menu bar, slim toolbar | Toolbar shows modes and transform tools, reflects the active tool |

**Visual hierarchy through restraint.**

- Use a dark, low-contrast neutral chrome so viewport content is the brightest, most saturated thing on screen.
- Reserve accent color for selection, active state and primary actions.
- Group with spacing and subtle separators instead of heavy frames.
- Use few font sizes and one consistent icon style.

**Density is a feature.** Editors are used for hours. Prefer compact, scannable rows (inspector rows around 20 to 24 px) over touch-sized targets, and provide a UI scale setting for high-DPI screens and accessibility.

**Context over modality.** The inspector shows whatever is selected and the toolbar reflects the active tool. Avoid dialogs that force a mode switch.

## UX priorities

In rough order of impact:

1. **Responsiveness.** The editor must stay interactive with a heavy scene. Keep asset loading, shader compilation and imports off the UI thread, show progress, and make long operations cancellable.
2. **Undo/redo for everything.** This is the biggest trust factor. Build a command system early: every edit (property change, transform, reparent, delete) is a command, and drag operations coalesce into one entry. Retrofitting this is painful.
3. **Direct manipulation.** Gizmos, drag-to-reorder, dragging assets into the viewport or onto fields, and click-drag scrubbing on numeric fields. Prefer acting on the object over filling in forms.
4. **Fast navigation and discoverability.** A command palette (Ctrl+P or Ctrl+K style) for actions, assets and settings; searchable outliner, asset browser and property lists; customizable shortcuts shown in tooltips; consistent right-click context menus.
5. **Immediate, visible feedback.** Selection and hover highlighting, live preview while dragging, and clear status for unsaved changes, build state and errors. Make errors actionable: clicking a log entry selects the offending object or opens the file.
6. **Progressive disclosure.** Common properties first, advanced ones in collapsible sections. Support multi-edit: selecting several objects shows shared properties with a mixed-value indicator.
7. **Crash safety.** Autosave, recovery on restart and non-destructive operations matter more than visual polish.
8. **Reflection-driven inspectors.** Generate property UI from reflection or metadata (ranges, tooltips, categories, widget hints). Hand-written inspector code per type does not scale and makes the UI inconsistent.

## Designs to avoid

| Avoid | Why | Do instead |
| --- | --- | --- |
| Modal dialogs for routine work | Blocks the viewport and breaks flow | Inline editing, popovers, non-blocking panels |
| Deep nested menus as the only path | Common actions end up 4+ clicks away | Toolbar, context menus, command palette |
| Fixed, non-dockable layouts | Breaks on ultrawide, multi-monitor and small screens | Dockable panels with saved layouts |
| Many floating windows | They pile up and cover the viewport | Docked tabs, tear-off only for multi-monitor |
| Inconsistent widgets | Vectors, colors and asset refs behaving differently erodes trust | One shared widget library used everywhere |
| Color-only information | Unreadable for colorblind users | Add icons or labels to status colors |
| Low-contrast dark themes | Gray-on-gray text is tiring over long sessions | Check contrast ratios, keep text legible |
| Hidden state | Invisible active tool, silent snapping, unlabeled local/world space | Surface every mode in the UI |
| Walls of text fields in the inspector | Slow to scan | Group, use icons, use fitting widgets (sliders, color pickers, curve editors) |
| Blocking the UI during imports or builds | Editor feels frozen | Background jobs with a visible task queue |
| "Are you sure?" confirmations | Interrupts flow and still permits mistakes | Make the action undoable |
| Over-customization too early | Theming and layout scripting before core workflows work | Solid core workflows first |

## ImGui-specific notes

The docking branch covers most of the layout goals above. These are the places where ImGui needs deliberate handling.

- **Dockspace and default layout.** Enable `ImGuiConfigFlags_DockingEnable` and create a dockspace over the main viewport. On first run (no saved ini), build the default layout with the DockBuilder API (`DockBuilderSplitNode`, `DockBuilderDockWindow`, `DockBuilderFinish`, from `imgui_internal.h`) so users never start with a pile of floating windows.
- **Layout persistence.** ImGui saves window and dock state to `imgui.ini`. For named layouts and presets, set `io.IniFilename = nullptr` and manage files yourself with `SaveIniSettingsToDisk` and `LoadIniSettingsFromDisk` (or the from-memory variants). Add a "Reset layout" action that re-runs the DockBuilder code.
- **Multi-viewport.** `ImGuiConfigFlags_ViewportsEnable` lets panels tear off into OS windows, and the GLFW and Vulkan backends support it. Each platform window gets its own swapchain, so test resize, minimize and monitor-DPI changes early. It is optional: ship docking first and add it once the rest is stable.
- **Viewport panel.** Render the scene to an offscreen image, register it with `ImGui_ImplVulkan_AddTexture`, and draw it with `ImGui::Image` inside the viewport window. When the panel size changes, recreate the render target (debounced), and destroy the old image only after the GPU has finished with it (frames in flight). Release descriptor sets with `ImGui_ImplVulkan_RemoveTexture`.
- **Input routing.** Only forward mouse and keyboard input to the camera or gizmo when the viewport window is hovered or focused, and respect `io.WantCaptureMouse` and `io.WantCaptureKeyboard`. Wrong routing is the most common cause of "typing in a text field moves the camera" bugs.
- **Undo via widget lifecycle.** ImGui widgets return `true` on every change while dragging. Use `IsItemActivated()` to capture the old value and `IsItemDeactivatedAfterEdit()` to push one command with old and new values, which coalesces a drag into a single undo step.
- **Stable IDs.** Panels, tree nodes and table rows need IDs that survive renames and reordering. Use `##` and `###` suffixes or push the entity ID rather than the display name, otherwise selection, scroll and open/closed state reset when a name changes.
- **Large lists.** Use `ImGuiListClipper` for the outliner, asset browser and log so cost scales with what is visible, not with scene size.
- **High DPI and fonts.** Query `glfwGetWindowContentScale` and scale both fonts and style (`ScaleAllSizes`). Check your ImGui version: recent releases support dynamic font sizing, while older ones need an atlas rebuild. Merge an icon font into the main font for consistent icons.
- **Idle behavior.** An editor does not need to redraw at full rate when nothing changes. Consider `glfwWaitEventsTimeout` while idle to save power and GPU time, and keep animating only while something is active.
- **Model/view separation.** Treat the UI as a thin view over an editor model (scene, selection, command stack). Widgets read and write through commands, never hold authoritative state. This also makes later play-in-editor and automated testing much easier.
- **UI testing.** Dear ImGui Test Engine can script clicks and checks against the real UI, which is worth considering once core panels exist.

## Practical suggestions and build order

Build the cross-cutting foundations before many panels, because retrofitting them touches every panel.

1. **Editor model and command/undo system.** Scene, selection and a command stack, with the UI only issuing commands.
2. **Property reflection and generic inspector.** Metadata-driven widgets with multi-edit and mixed-value display.
3. **Docked layout shell.** Default DockBuilder layout, save/restore, reset.
4. **Viewport panel.** Offscreen target, resize handling, input routing, gizmos, selection highlight.
5. **Outliner and asset browser.** Virtualized lists, search and filter, drag and drop.
6. **Command palette and shortcut system.** Searchable actions with customizable bindings.
7. **Background jobs and progress UI.** Task queue for imports, builds and shader compiles.
8. **Autosave and crash recovery.**
9. **Polish.** Theme, icons, DPI scaling, multi-viewport.

Two further recommendations:

- **Decouple the editor from the renderer.** Keep the editor talking to the engine through commands and the scene model, not by touching GPU state, matching the existing RHI/Renderer split. Decide early whether play-in-editor uses a separate world copy, since it affects the architecture. This is still open, so both paths: with a separate world copy, Play clones the scene, runs the simulation on the copy and discards it on Stop, which keeps editor state untouched but needs cheap scene cloning; with standalone-only, Play launches a separate game process, which keeps the editor simpler but slows iteration and needs an IPC channel for live inspection. Keeping the editor model separate from the runtime world leaves both options available.
- **Dogfood one real workflow** (place objects, tweak a material, save, reload) and polish that loop before adding features. Test at realistic scale, with thousands of entities and large asset directories, since that is where naive UIs fall over.
