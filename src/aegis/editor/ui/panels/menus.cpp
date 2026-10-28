module;

#include <imgui.h>

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
// Creates the entity 'name' with 'components' under 'parent', selects it and asks for its name to be typed.
void createAndSelect(EditorContext& editor, FrameContext& frame, std::string name, std::span<const reflect::TypeInfo* const> components, world::Entity parent)
{
    auto created = createEntity(editWorld(editor, frame), editor.undoStack(), name, parent, components);
    if (!created)
    {
        log::warn("Cannot create '{}': {}", name, created.error());
        return;
    }
    editor.select(*created);
    editor.requestRename();
}
}

auto pickType(std::span<const reflect::TypeInfo* const> types) -> const reflect::TypeInfo*
{
    if (types.empty())
        ImGui::TextDisabled("Nothing to add");

    const reflect::TypeInfo* picked = nullptr;
    for (std::size_t first = 0; first < types.size();)
    {
        const auto& category = types[first]->meta.category;
        std::size_t end = first;
        while (end < types.size() && types[end]->meta.category == category)
            ++end;

        if (category.empty() || ImGui::BeginMenu(category.c_str()))
        {
            for (std::size_t i = first; i < end; ++i)
            {
                const std::string label{ types[i]->displayName() };
                if (ImGui::MenuItem(label.c_str()))
                    picked = types[i];
            }
            if (!category.empty())
                ImGui::EndMenu();
        }
        first = end;
    }
    return picked;
}

void drawCreateMenu(EditorContext& editor, FrameContext& frame, world::Entity parent)
{
    if (ImGui::MenuItem("Empty"))
        createAndSelect(editor, frame, "Empty", {}, parent);

    // An entity with one registered component, named for the menu item; disabled if the game doesn't register the type
    const auto preset = [&](const char* label, const char* type) {
        const auto* info = frame.types.find(type);
        if (ImGui::MenuItem(label, nullptr, false, info != nullptr))
            createAndSelect(editor, frame, label, std::span{ &info, 1 }, parent);
    };
    preset("Camera", "Camera");
    preset("Point Light", "PointLight");
    preset("Directional Light", "DirectionalLight");

    ImGui::Separator();
    // Its own ID scope: the category "Camera" would share an ID with the item "Camera" above
    ImGui::PushID("components");
    const auto* type = pickType(creatableComponents(frame.types, editor.world()->registry()));
    ImGui::PopID();
    if (type)
        createAndSelect(editor, frame, std::string{ type->displayName() }, std::span{ &type, 1 }, parent);
}
}
