module;

#include <cfloat>

#include <imgui.h>

#include "aegis/editor/ui/theme.h"

module aegis.editor;
import std;
import aegis.paths;
import :panels;

namespace aegis::editor
{
namespace
{
// A font the editor bundles, with the license file next to it in the engine content's `fonts/`.
struct BundledFont
{
    const char* name;
    const char* license;
    const char* file;
};

constexpr std::array bundledFonts{
    BundledFont{ "Inter", "SIL Open Font License 1.1", "Inter-OFL.txt" },
    BundledFont{ "JetBrains Mono", "SIL Open Font License 1.1", "JetBrainsMono-OFL.txt" },
    BundledFont{ "Lucide (icons)", "ISC License", "Lucide-LICENSE.txt" },
};

auto readText(const std::filesystem::path& file) -> std::string
{
    std::ifstream stream{ file, std::ios::binary };
    if (!stream)
        return std::format("Cannot read '{}'", file.string());
    return std::string{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
}

// Help > About: the engine's name and version, and the bundled fonts with their licenses, in a floating window of its
// own. A license's text is read when its entry is first opened.
class AboutPanel final : public Panel
{
public:
    [[nodiscard]] auto name() const -> std::string_view override { return "About"; }

    void draw(EditorContext&, FrameContext&) override
    {
        bool open = true;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2{ 0.5f, 0.5f });
        const float width = 520.0f * theme::scale(); // The licenses wrap at it
        ImGui::SetNextWindowSizeConstraints(ImVec2{ width, 0.0f }, ImVec2{ width, FLT_MAX });
        const bool visible = ImGui::Begin("About", &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings);
        setOpen(open);
        if (!visible)
        {
            ImGui::End();
            return;
        }

        ImGui::Text("Aegis Engine %s", AEGIS_VERSION);
        ImGui::TextDisabled("A C++23 Vulkan render engine");

        ImGui::SeparatorText("Fonts");
        for (std::size_t i = 0; i < bundledFonts.size(); ++i)
        {
            const auto& font = bundledFonts[i];
            if (!ImGui::TreeNode(font.file, "%s  —  %s", font.name, font.license))
                continue;
            if (m_licenses[i].empty())
                m_licenses[i] = readText(paths::content() / "fonts" / font.file);
            if (ImGui::BeginChild(font.file, ImVec2{ 0.0f, 200.0f * theme::scale() }, ImGuiChildFlags_Borders))
            {
                theme::MonospaceScope mono;
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(m_licenses[i].c_str());
                ImGui::PopTextWrapPos();
            }
            ImGui::EndChild();
            ImGui::TreePop();
        }
        ImGui::End();
    }

private:
    std::array<std::string, bundledFonts.size()> m_licenses; // Read on first view
};
}

auto makeAboutPanel() -> std::unique_ptr<Panel>
{
    return std::make_unique<AboutPanel>();
}
}
