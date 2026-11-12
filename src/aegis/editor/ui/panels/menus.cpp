module;

#include <imgui.h>

#include "aegis/editor/ui/theme.h"

module aegis.editor;
import std;
import aegis.core;
import aegis.reflect;
import aegis.world;
import :panels;
import :theme;

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

void drawCreateMenu(EditorContext& editor, FrameContext& frame, world::Entity parent)
{
    // One click creates any of them: the components by category without submenus, the categories a separator apart and
    // each item marked by its component's icon
    if (ImGui::MenuItem(std::format("{} Empty", theme::icons::empty).c_str()))
        createAndSelect(editor, frame, "Empty", {}, parent);
    const std::string* category = nullptr;
    for (const auto* type : creatableComponents(frame.types, editor.world()->registry()))
    {
        if (!category || *category != type->meta.category)
        {
            category = &type->meta.category;
            ImGui::Separator();
        }
        const std::string name{ type->displayName() };
        ImGui::PushID(type->name.c_str());
        if (ImGui::MenuItem(std::format("{} {}", componentIcon(*type), name).c_str()))
            createAndSelect(editor, frame, name, std::span{ &type, 1 }, parent);
        ImGui::PopID();
    }
}
}
