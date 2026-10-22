module aegis.editor:panels;
import std;
import :panel;

namespace aegis::editor
{
// The built-in panels, in the View menu's order; each is defined in its own .cpp (ui/panels/).
[[nodiscard]] auto makeScenePanel() -> std::unique_ptr<Panel>;
[[nodiscard]] auto makeRendererPanel() -> std::unique_ptr<Panel>;
[[nodiscard]] auto makeStatisticsPanel() -> std::unique_ptr<Panel>;
[[nodiscard]] auto makeProfilerPanel() -> std::unique_ptr<Panel>;
[[nodiscard]] auto makeDemoPanel() -> std::unique_ptr<Panel>;
}
