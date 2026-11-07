module aegis.editor;
import std;
import aegis.core;
import aegis.reflect;
import aegis.world;
import :panels;

namespace aegis::editor
{
namespace
{
// The entry's edits, one side of each, written to the world.
auto apply(std::span<const Edit> edits, EditSide side, const EditorContext& editor, FrameContext& frame) -> bool
{
    const auto failures = applyEdits(editWorld(editor, frame), edits, side);
    for (const auto& failure : failures)
        log::warn("Cannot {} {}", side == EditSide::Old ? "undo" : "redo", failure);
    return failures.empty();
}
}

auto editWorld(const EditorContext& editor, FrameContext& frame) -> EditWorld
{
    return EditWorld{
        .types = frame.types,
        .registry = editor.world()->registry(),
        .prepare = [&frame](const reflect::TypeInfo& type, void* object) { frame.assets.resolve(type, object, frame.types); },
    };
}

auto undoEdit(EditorContext& editor, FrameContext& frame) -> bool
{
    const auto edits = editor.undoStack().undo();
    return !edits.empty() && apply(edits, EditSide::Old, editor, frame);
}

auto redoEdit(EditorContext& editor, FrameContext& frame) -> bool
{
    const auto edits = editor.undoStack().redo();
    return !edits.empty() && apply(edits, EditSide::New, editor, frame);
}
}
