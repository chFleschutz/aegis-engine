export module aegis.editor.core:side_rail;
import std;

export namespace aegis::editor
{
/// @brief Whether the right column is folded into the side rail, and which column panel (the Hierarchy, the Inspector)
///        the rail's flyout shows over the viewport's right edge. The user folds and unfolds it (Ctrl+B, the menu bar's
///        button, the View menu); the layout file keeps it. Panels are named by their window titles.
class SideRail
{
public:
    [[nodiscard]] auto folded() const -> bool { return m_folded; }
    /// @brief The panel the flyout shows; none while it is closed, always none while unfolded.
    [[nodiscard]] auto flyout() const -> std::optional<std::string_view>;
    [[nodiscard]] auto isShown(std::string_view panel) const -> bool { return m_flyout == panel; }

    /// @brief Folds or unfolds the column; either closes the flyout.
    void setFolded(bool folded);
    void toggleFolded() { setFolded(!m_folded); }

    /// @brief Shows 'panel' in the flyout instead of the one it shows; false while unfolded, which shows none.
    auto show(std::string_view panel) -> bool;
    /// @brief Closes the flyout if it shows 'panel', and shows 'panel' otherwise (show).
    auto toggle(std::string_view panel) -> bool;
    void close() { m_flyout.reset(); }

    /// @brief The folded state as the layout file keeps it: "Folded=1" or "Folded=0".
    [[nodiscard]] auto settingsLine() const -> std::string;
    /// @brief Reads a line settingsLine wrote; any other line is ignored.
    void readSettingsLine(std::string_view line);

private:
    bool m_folded{ false };
    std::optional<std::string> m_flyout;
};
}
