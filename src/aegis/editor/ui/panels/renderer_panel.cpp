module;

#include <imgui.h>

#include "aegis/editor/ui/theme.h"

module aegis.editor;
import std;
import aegis.render_graph;
import aegis.renderer;
import aegis.rhi;
import :panels;

namespace aegis::editor
{
namespace
{
auto toString(render_graph::ResourceDescription::Kind kind) -> const char*
{
    using Kind = render_graph::ResourceDescription::Kind;
    switch (kind)
    {
    case Kind::Texture: return "Texture";
    case Kind::Buffer: return "Buffer";
    case Kind::Reference: return "Reference";
    }
    return "Unknown";
}

// The renderer's extent, the frame graph's passes (with their settings) and its resources.
class RendererPanel final : public Panel
{
public:
    [[nodiscard]] auto name() const -> std::string_view override { return "Renderer"; }
    [[nodiscard]] auto debug() const -> bool override { return true; }

    void draw(EditorContext&, FrameContext& frame) override
    {
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 10.0f * theme::scale(), 30.0f * theme::scale()), ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
        bool open = true;
        const bool visible = ImGui::Begin("Renderer", &open);
        setOpen(open);
        if (!visible)
        {
            ImGui::End();
            return;
        }

        auto& frameGraph = frame.frameGraph;
        auto& pool = frameGraph.pool();

        // Renderer info
        ImGui::SeparatorText("Renderer Info");
        const auto extent = frame.renderer.extent();
        ImGui::Text("Extent: %d x %d", extent.x, extent.y);
        ImGui::Text("Aspect Ratio: %.2f", static_cast<float>(extent.x) / static_cast<float>(extent.y));
        ImGui::NewLine();

        // Frame graph render passes
        ImGui::SeparatorText("Render Passes");
        int i = 0;
        for (const auto& description : frameGraph.describePasses())
        {
            ImGui::PushID(i++);
            const std::string passName{ description.name };
            if (ImGui::CollapsingHeader(passName.c_str(), ImGuiTreeNodeFlags_None))
            {
                description.pass->drawUI();
                for (const auto& [label, handles] : { std::pair{ "Reads", &description.reads }, std::pair{ "Writes", &description.writes } })
                {
                    if (handles->empty() || !ImGui::TreeNode(label))
                        continue;
                    for (const auto handle : *handles)
                    {
                        const std::string name{ pool.name(handle) };
                        ImGui::BulletText("%s", name.c_str());
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::PopID();
        }
        ImGui::NewLine();

        // Frame graph resources
        ImGui::SeparatorText("Frame Graph Resources");
        const ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterH;
        const ImVec2 outerSize = ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 10);
        if (ImGui::BeginTable("Resources", 4, flags, outerSize))
        {
            ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();

            int id = 0;
            for (const auto& resource : pool.describe())
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%d", id++);
                ImGui::TableSetColumnIndex(1);
                const std::string name{ resource.name };
                ImGui::Text("%s", name.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s", toString(resource.kind));
                ImGui::TableSetColumnIndex(3);
                if (resource.format)
                {
                    const std::string format{ rhi::formatName(*resource.format) };
                    ImGui::Text("%s", format.c_str());
                }
            }
            ImGui::EndTable();
        }

        ImGui::End();
    }
};
}

auto makeRendererPanel() -> std::unique_ptr<Panel>
{
    return std::make_unique<RendererPanel>();
}
}
