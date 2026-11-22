module aegis.editor;
import std;
import aegis.assets;
import aegis.core;
import aegis.math;
import aegis.reflect;
import aegis.world;
import :edits;

namespace aegis::editor
{
namespace
{
auto nameOf(world::Registry& registry, world::Entity entity) -> const char*
{
    return registry.has<Name>(entity) ? registry.get<Name>(entity).name.c_str() : "Entity";
}

// The entry's edits, one side of each, written to the world.
auto apply(std::span<const Edit> edits, EditSide side, const EditorContext& editor, FrameContext& frame) -> bool
{
    const auto failures = applyEdits(editWorld(editor, frame), edits, side);
    for (const auto& failure : failures)
        log::warn("Cannot {} {}: {}", side == EditSide::Old ? "undo" : "redo", failure.edit, describe(failure.error));
    return failures.empty();
}
}

auto resolveAssets(FrameContext& frame) -> PrepareField
{
    return [&frame](const reflect::TypeInfo& type, void* object) { frame.assets.resolve(type, object, frame.types); };
}

auto editWorld(const EditorContext& editor, FrameContext& frame) -> EditWorld
{
    return EditWorld{
        .types = frame.types,
        .registry = editor.world()->registry(),
        .prepare = resolveAssets(frame),
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

auto duplicateSelected(EditorContext& editor, FrameContext& frame) -> bool
{
    if (!editor.world())
        return false;
    auto& registry = editor.world()->registry();
    const auto roots = selectedRoots(registry, editor.selection());
    if (roots.empty())
        return false;
    auto& history = editor.undoStack();
    std::optional<UndoGroup> group;
    if (roots.size() > 1)
        group.emplace(history, std::format("Duplicate {} entities", roots.size()));

    std::vector<StableId> copies;
    for (const auto root : roots)
    {
        if (auto copy = duplicateEntity(editWorld(editor, frame), history, root))
            copies.push_back(registry.get<StableId>(*copy));
        else
            log::warn("Cannot duplicate '{}': {}", nameOf(registry, root), describe(copy.error()));
    }
    if (copies.empty())
        return false;
    editor.selection().assign(copies);
    return true;
}

auto destroySelected(EditorContext& editor, FrameContext& frame) -> bool
{
    if (!editor.world())
        return false;
    auto& registry = editor.world()->registry();
    const auto roots = selectedRoots(registry, editor.selection());
    if (roots.empty())
        return false;
    auto& history = editor.undoStack();
    std::optional<UndoGroup> group;
    if (roots.size() > 1)
        group.emplace(history, std::format("Delete {} entities", roots.size()));

    bool destroyed = false;
    for (const auto root : roots)
    {
        const std::string name = nameOf(registry, root);
        if (auto result = destroyEntity(editWorld(editor, frame), history, root))
            destroyed = true;
        else
            log::warn("Cannot destroy '{}': {}", name, describe(result.error()));
    }
    editor.selection().clear();
    return destroyed;
}

auto instantiateAsset(EditorContext& editor, FrameContext& frame, std::uint64_t asset, world::Entity parent, std::optional<glm::vec3> location) -> bool
{
    auto& registry = editor.world()->registry();
    const auto edits = editWorld(editor, frame);
    const auto create = [&](world::Entity at) -> std::expected<world::Entity, Error> {
        auto holder = frame.assets.instantiateScene(*editor.world(), frame.types, assets::AssetId{ asset }, at);
        const auto* transformType = frame.types.find<Transform>();
        if (holder && location && transformType)
        {
            const FieldValues values{ { .field = "location", .value = reflect::Value{ *location } } };
            if (auto written = writeComponent(*transformType, registry, *holder, &registry.get<Transform>(*holder), values, edits.prepare); !written)
                log::warn("Cannot place the instance: {}", reflect::describe(written.error()));
        }
        if (!holder)
            return makeAssetError(std::move(holder.error()), "cannot instantiate the scene");
        return *holder;
    };
    auto holder = instantiateEntity(edits, editor.undoStack(), parent, create);
    if (!holder)
    {
        log::warn("Cannot instantiate {}: {}", assets::AssetId{ asset }, describe(holder.error()));
        return false;
    }
    editor.select(*holder);
    return true;
}
}
