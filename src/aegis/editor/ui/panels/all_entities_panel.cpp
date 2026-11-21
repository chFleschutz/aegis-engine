module;

#include <imgui.h>

module aegis.editor;
import std;
import aegis.world;
import :panels;
import :theme;

namespace aegis::editor
{
namespace
{
// Every entity of the world as one flat list, whatever its parent: for finding what the hierarchy hides.
class AllEntitiesPanel final : public Panel
{
public:
    [[nodiscard]] auto name() const -> std::string_view override { return "All Entities"; }
    [[nodiscard]] auto icon() const -> const char* override { return theme::icons::allEntities; }
    [[nodiscard]] auto placement() const -> Placement override { return Placement::Float; }
    [[nodiscard]] auto menu() const -> Menu override { return Menu::Debug; }

    void draw(EditorContext& editor, FrameContext&) override
    {
        auto& registry = editor.world()->registry();
        const auto view = registry.view<StableId>();
        ImGuiListClipper clipper; // Only the entities in view are drawn: the view is indexed, not walked
        clipper.Begin(static_cast<int>(view.size()));
        while (clipper.Step())
        {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
            {
                const world::Entity entity{ view.begin()[index] };
                const auto id = registry.get<StableId>(entity);
                ImGui::PushID(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(id.value)));
                if (ImGui::Selectable(registry.has<Name>(entity) ? registry.get<Name>(entity).name.c_str() : "Entity", editor.isSelected(entity)))
                    editor.select(entity);
                ImGui::PopID();
            }
        }
    }
};
}

auto makeAllEntitiesPanel() -> std::unique_ptr<Panel>
{
    return std::make_unique<AllEntitiesPanel>();
}
}
