module;

#include <imgui.h>

module aegis.editor;
import std;
import aegis.world;
import :panels;

namespace aegis::editor
{
namespace
{
// Every entity of the world as one flat list, whatever its parent: for finding what the hierarchy hides.
class AllEntitiesPanel final : public Panel
{
public:
    [[nodiscard]] auto name() const -> std::string_view override { return "All Entities"; }
    [[nodiscard]] auto debug() const -> bool override { return true; }

    void draw(EditorContext& editor, FrameContext&) override
    {
        bool open = true;
        const bool visible = ImGui::Begin("All Entities", &open);
        setOpen(open);
        if (!visible)
        {
            ImGui::End();
            return;
        }

        auto& registry = editor.world()->registry();
        for (const auto handle : registry.view<StableId>())
        {
            const world::Entity entity{ handle };
            const auto id = registry.get<StableId>(entity);
            const std::string label = std::format("{}##{}", registry.has<Name>(entity) ? registry.get<Name>(entity).name : std::string{ "Entity" }, id.value);
            if (ImGui::Selectable(label.c_str(), editor.isSelected(entity)))
                editor.select(entity);
        }
        ImGui::End();
    }
};
}

auto makeAllEntitiesPanel() -> std::unique_ptr<Panel>
{
    return std::make_unique<AllEntitiesPanel>();
}
}
