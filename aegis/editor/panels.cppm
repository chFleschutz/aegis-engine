module aegis.editor:panels;
import std;
import :actions;
import :panel;

namespace aegis::editor
{
// The built-in panels' names (Panel::name), for the few places that address one of them.
namespace panels
{
inline constexpr std::string_view scene = "Scene"; // Its window is titled "Hierarchy"
inline constexpr std::string_view inspector = "Inspector";
inline constexpr std::string_view content = "Content";
inline constexpr std::string_view console = "Console";
inline constexpr std::string_view material = "Material";
inline constexpr std::string_view preferences = "Preferences";
inline constexpr std::string_view shortcuts = "Keyboard Shortcuts";
inline constexpr std::string_view about = "About";
}

// The built-in panels, in their menus' order; each is defined in its own .cpp (ui/panels/). The dialogs are not
// listed in a menu: Edit > Preferences... and the Help menu open them.
// The hierarchy; its menus show the actions' labels and shortcuts, so 'actions' must outlive it.
[[nodiscard]] auto makeScenePanel(ActionRegistry& actions) -> std::unique_ptr<Panel>;
[[nodiscard]] auto makeAllEntitiesPanel() -> std::unique_ptr<Panel>;
[[nodiscard]] auto makeInspectorPanel() -> std::unique_ptr<Panel>;
[[nodiscard]] auto makeContentPanel() -> std::unique_ptr<Panel>;
[[nodiscard]] auto makeConsolePanel() -> std::unique_ptr<Panel>;
[[nodiscard]] auto makeMaterialPanel() -> std::unique_ptr<Panel>;
[[nodiscard]] auto makePreferencesPanel() -> std::unique_ptr<Panel>;
// The shortcuts of 'actions', which must outlive it, and the viewport's.
[[nodiscard]] auto makeShortcutsPanel(const ActionRegistry& actions) -> std::unique_ptr<Panel>;
[[nodiscard]] auto makeAboutPanel() -> std::unique_ptr<Panel>;
[[nodiscard]] auto makeRendererPanel() -> std::unique_ptr<Panel>;
[[nodiscard]] auto makeStatisticsPanel() -> std::unique_ptr<Panel>;
[[nodiscard]] auto makeProfilerPanel() -> std::unique_ptr<Panel>;
}
