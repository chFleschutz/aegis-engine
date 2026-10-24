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
// The edit's side written to the world.
auto apply(const Edit& edit, EditSide side, FrameContext& frame) -> bool
{
    if (auto applied = applyEdit(editWorld(frame), edit, side); !applied)
    {
        log::warn("Cannot {} {}: {}", side == EditSide::Old ? "undo" : "redo", describe(edit), applied.error());
        return false;
    }
    return true;
}
}

auto editWorld(FrameContext& frame) -> EditWorld
{
    return EditWorld{
        .types = frame.types,
        .registry = frame.world.registry(),
        .prepare = [&frame](const reflect::TypeInfo& type, void* object) { frame.assets.resolve(type, object, frame.types); },
    };
}

auto undoEdit(EditorContext& editor, FrameContext& frame) -> bool
{
    const auto* edit = editor.undoStack().undo();
    return edit && apply(*edit, EditSide::Old, frame);
}

auto redoEdit(EditorContext& editor, FrameContext& frame) -> bool
{
    const auto* edit = editor.undoStack().redo();
    return edit && apply(*edit, EditSide::New, frame);
}
}
