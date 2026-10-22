module;

#include <imgui.h>

module aegis.editor;
import std;
import :panels;

namespace aegis::editor
{
namespace
{
// ImGui's demo window: every widget, for reference while building panels.
class DemoPanel final : public Panel
{
public:
    [[nodiscard]] auto name() const -> std::string_view override { return "ImGui Demo"; }

    void draw(EditorContext&, FrameContext&) override { ImGui::ShowDemoWindow(); }
};
}

auto makeDemoPanel() -> std::unique_ptr<Panel>
{
    return std::make_unique<DemoPanel>();
}
}
