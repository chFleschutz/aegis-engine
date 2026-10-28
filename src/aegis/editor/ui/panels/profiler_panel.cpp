module;

#include <imgui.h>

module aegis.editor;
import std;
import aegis.core;
import aegis.render_graph;
import :panels;

namespace aegis::editor
{
namespace
{
// The frame time, the CPU scopes (Profiler) and the frame graph's GPU pass timings.
class ProfilerPanel final : public Panel
{
public:
    [[nodiscard]] auto name() const -> std::string_view override { return "Profiler"; }
    [[nodiscard]] auto debug() const -> bool override { return true; }

    void draw(EditorContext&, FrameContext& frame) override
    {
        ImGui::SetNextWindowPos(ImVec2(10, ImGui::GetIO().DisplaySize.y - 10), ImGuiCond_FirstUseEver, ImVec2(0, 1));
        bool open = true;
        const bool visible = ImGui::Begin("Profiler", &open);
        setOpen(open);
        if (!visible)
        {
            ImGui::End();
            return;
        }

        auto& profiler = Profiler::instance();
        const double frameTime = profiler.time("Frame Time");

        ImGui::Text("Frame Time: %.3f ms", frameTime);
        ImGui::Text("FPS: %.1f", frameTime > 0.0 ? 1000.0 / frameTime : 0.0);

        ImGui::Spacing();

        const ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterH;
        const ImVec2 outerSize = ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 10);
        if (ImGui::BeginTable("CPU Times", 3, flags, outerSize))
        {
            ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
            ImGui::TableSetupColumn("CPU Time", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Frame Percent (%)", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();

            for (const auto& [name, rolling] : profiler.times())
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%7.3f", rolling.average());
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%7.2f", frameTime > 0.0 ? rolling.average() / frameTime * 100.0 : 0.0);
            }

            ImGui::EndTable();
        }

        ImGui::Spacing();

        if (ImGui::BeginTable("GPU Times", 3, flags, outerSize))
        {
            ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
            ImGui::TableSetupColumn("GPU Time", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Frame Percent (%)", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();

            for (const auto& timing : frame.frameGraph.timings())
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", timing.name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%7.3f", timing.gpuMs);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%7.2f", frameTime > 0.0 ? timing.gpuMs / frameTime * 100.0 : 0.0);
            }
            ImGui::EndTable();
        }

        ImGui::End();
    }
};
}

auto makeProfilerPanel() -> std::unique_ptr<Panel>
{
    return std::make_unique<ProfilerPanel>();
}
}
