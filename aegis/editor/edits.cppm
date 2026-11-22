module aegis.editor:edits;
import std;
import aegis.editor.core;
import aegis.engine;
import aegis.math;
import aegis.world;

// The changes the menus, the shortcuts and the panels make to the world the panels show, each as one undo entry, and
// the edit history's steps.
namespace aegis::editor
{
/// @brief Resolves the asset references of a component or field just written, through the frame's asset library: the
///        PrepareField of the edits, the play copy and the scenes the editor instantiates.
[[nodiscard]] auto resolveAssets(FrameContext& frame) -> PrepareField;

/// @brief The world the panels show (the edit world, or the play world while playing) as the edits see it; the asset library resolves the references of written components.
[[nodiscard]] auto editWorld(const EditorContext& editor, FrameContext& frame) -> EditWorld;

/// @brief Steps the edit history back (or forward) and writes the value to the world; false if there is nothing to do or
///        the edit no longer applies (logged).
auto undoEdit(EditorContext& editor, FrameContext& frame) -> bool;
auto redoEdit(EditorContext& editor, FrameContext& frame) -> bool;

/// @brief Duplicates the selected roots (selectedRoots) as one undo entry and selects the copies; false if nothing is
///        selected or every copy failed (logged).
auto duplicateSelected(EditorContext& editor, FrameContext& frame) -> bool;

/// @brief Destroys the selected roots (selectedRoots) and their descendants as one undo entry and clears the
///        selection; false if nothing is selected or every destroy failed (logged).
auto destroySelected(EditorContext& editor, FrameContext& frame) -> bool;

/// @brief Instantiates the scene or model 'asset' under 'parent' (a root for a null one), at 'location' if given,
///        undoably, and selects it; false if it failed (logged).
auto instantiateAsset(EditorContext& editor, FrameContext& frame, std::uint64_t asset, world::Entity parent, std::optional<math::Float3> location = std::nullopt) -> bool;
}
