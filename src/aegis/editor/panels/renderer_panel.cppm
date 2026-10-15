module;

#include <imgui.h>

#include <string>
#include <utility>

export module aegis.editor.panels:renderer_panel;

import aegis.render_graph;
import aegis.renderer;
import aegis.rhi;

export namespace aegis::editor
{
class RendererPanel
{
public:
    static auto toString(aegis::render_graph::ResourceDescription::Kind kind) -> const char*
    {
        using Kind = aegis::render_graph::ResourceDescription::Kind;
        switch (kind)
        {
        case Kind::Texture: return "Texture";
        case Kind::Buffer: return "Buffer";
        case Kind::Reference: return "Reference";
        }
        return "Unknown";
    }

    void draw(aegis::renderer::Renderer& renderer, aegis::render_graph::FrameGraph& frameGraph)
    {
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 10, 30), ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
        if (!ImGui::Begin("Renderer"))
        {
            ImGui::End();
            return;
        }

        auto& pool = frameGraph.pool();

        // Renderer info
        ImGui::SeparatorText("Renderer Info");
        const auto extent = renderer.extent();
        ImGui::Text("Extent: %d x %d", extent.x, extent.y);
        ImGui::Text("Aspect Ratio: %.2f", static_cast<float>(extent.x) / static_cast<float>(extent.y));
        ImGui::NewLine();

        // Frame graph render passes
        ImGui::SeparatorText("Render Passes");
        int i = 0;
        for (auto* pass : frameGraph.passes())
        {
            ImGui::PushID(i++);
            const auto info = pass->info();
            if (ImGui::CollapsingHeader(info.name.c_str(), ImGuiTreeNodeFlags_None))
            {
                pass->drawUI();
                for (const auto& [label, handles] : { std::pair{ "Reads", &info.reads }, std::pair{ "Writes", &info.writes } })
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
                    const std::string format{ aegis::rhi::formatName(*resource.format) };
                    ImGui::Text("%s", format.c_str());
                }
            }
            ImGui::EndTable();
        }

        ImGui::End();
    }
};
}
