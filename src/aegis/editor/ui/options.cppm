export module aegis.editor:options;
import std;
import aegis.engine;

export namespace aegis::editor
{
/// @brief The editor's command-line options, which the launcher leaves to the shell (AppDesc::shellOptions). They set
///        up a test or a screenshot.
struct EditorOptions
{
    /// @brief --editor-panels all: open every docked panel at startup (not the ImGui demo) and pin the drawer panels.
    bool allPanels{ false };
    /// @brief --editor-select <name>: select the first entity of that Name once the scene has loaded; empty selects none.
    std::string select;
    /// @brief --editor-play: enter play after the first frame.
    bool play{ false };
    /// @brief --editor-full-viewport: start in full viewport.
    bool fullViewport{ false };
    /// @brief --editor-fold: start with the right column folded into the side rail and the Inspector in its flyout.
    bool fold{ false };
};

/// @brief The editor's options for the usage line (usage's 'shellOptions').
inline constexpr std::string_view optionsUsage{ "[--editor-panels all] [--editor-select <name>] [--editor-play] [--editor-full-viewport] [--editor-fold]" };

/// @brief Reads the editor's options.
/// @return EngineErrorCode::InvalidArguments naming the first option that is unknown, lacks its value or has one it
///         doesn't take.
[[nodiscard]] auto parseOptions(std::span<const ShellOption> options) -> std::expected<EditorOptions, EngineError>;
}
