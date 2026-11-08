export module aegis.editor.core:drawers;
import std;

export namespace aegis::editor
{
/// @brief Which drawer panel the drawer shows and which ones are pinned. A drawer panel (Assets, Console, the debug
///        panels) opens in the drawer, a window over the bottom of the viewport that shows one of them at a time, or,
///        pinned, docks into a bottom node of the layout instead. Panels are named by their window titles.
class Drawers
{
public:
    /// @brief The panel the drawer shows; none while it is closed.
    [[nodiscard]] auto shown() const -> std::optional<std::string_view>;
    [[nodiscard]] auto isShown(std::string_view panel) const -> bool { return m_shown == panel; }
    [[nodiscard]] auto isPinned(std::string_view panel) const -> bool;
    /// @brief The pinned panels, in the order they were pinned.
    [[nodiscard]] auto pinned() const -> std::span<const std::string> { return m_pinned; }

    /// @brief Shows 'panel' in the drawer instead of the one it shows; false for a pinned panel, which shows where it
    ///        is docked and leaves the drawer as it is.
    auto show(std::string_view panel) -> bool;
    /// @brief Closes the drawer if it shows 'panel', and shows 'panel' otherwise (show).
    auto toggle(std::string_view panel) -> bool;
    /// @brief Closes the drawer if it shows 'panel' (it was closed).
    void hide(std::string_view panel);
    void close() { m_shown.reset(); }

    /// @brief Docks 'panel' into the layout; the drawer closes if it showed it.
    void pin(std::string_view panel);
    /// @brief Returns 'panel' to the drawer, which shows it.
    void unpin(std::string_view panel);
    /// @brief Unpins every panel and closes the drawer (View > Reset Layout).
    void reset();

    /// @brief The pinned panels as the layout file keeps them: "Pinned=Assets,Console".
    [[nodiscard]] auto settingsLine() const -> std::string;
    /// @brief Reads a line settingsLine wrote; any other line is ignored, and so are empty names.
    void readSettingsLine(std::string_view line);

private:
    std::optional<std::string> m_shown;
    std::vector<std::string> m_pinned;
};
}
