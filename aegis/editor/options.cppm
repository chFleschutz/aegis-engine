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
    /// @brief --editor-stop <frames>: stop play, as a click on Stop does, once it has run that many frames (at least 1).
    std::optional<int> stopAfter;
    /// @brief --editor-full-viewport: start in full viewport.
    bool fullViewport{ false };
    /// @brief --editor-fold: start with the right column folded into the side rail and the Inspector in its flyout.
    bool fold{ false };
    /// @brief --editor-check-stats: check that during play the statistics count the play world (StatisticsCheck).
    bool checkStats{ false };
};

/// @brief The editor's options for the help text (helpText's 'shellOptions').
inline constexpr std::array optionsHelp{
    OptionHelp{ "--editor-panels all", "Open every docked panel at startup and pin the drawer panels." },
    OptionHelp{ "--editor-select <name>", "Select the first entity of this name once the scene has loaded." },
    OptionHelp{ "--editor-play", "Enter play after the first frame." },
    OptionHelp{ "--editor-stop <frames>", "Stop play once it has run this many frames (a test, with --editor-play)." },
    OptionHelp{ "--editor-full-viewport", "Start in full viewport." },
    OptionHelp{ "--editor-fold", "Start with the right column folded into the side rail and the Inspector\nin its flyout." },
    OptionHelp{ "--editor-check-stats", "Check that during play the statistics count the play world (a test,\nwith --editor-play)." },
};

/// @brief Reads the editor's options.
/// @return EngineErrorCode::InvalidArguments naming the first option that is unknown, lacks its value or has one it
///         doesn't take.
[[nodiscard]] auto parseOptions(std::span<const ShellOption> options) -> std::expected<EditorOptions, EngineError>;
}
