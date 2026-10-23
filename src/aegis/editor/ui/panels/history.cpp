module aegis.editor;
import std;
import aegis.core;
import aegis.reflect;
import aegis.scripting.script;
import aegis.world;
import :panels;

namespace aegis::editor
{
namespace
{
// The edit's value written to the world; scripts are found through the entity's Scripts.
auto apply(const FieldEdit& edit, EditSide side, FrameContext& frame) -> bool
{
    const auto prepare = [&frame](const reflect::TypeInfo& type, void* object) { frame.assets.resolve(type, object, frame.types); };
    const FindScript findScript = [](const reflect::TypeInfo& type, world::Registry& registry, world::Entity entity, std::size_t index) -> void* {
        if (!registry.has<scripting::Scripts>(entity))
            return nullptr;
        const auto& entries = registry.get<scripting::Scripts>(entity).entries;
        return index < entries.size() && entries[index].name == type.name ? entries[index].object : nullptr;
    };
    if (auto applied = applyEdit(frame.types, frame.world.registry(), edit, side, prepare, findScript); !applied)
    {
        log::warn("Cannot {} the edit of {}.{}: {}", side == EditSide::Old ? "undo" : "redo", edit.type, edit.field, reflect::describe(applied.error()));
        return false;
    }
    return true;
}
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
