export module aegis.editor:panel;
import std;
import aegis.core;
import aegis.editor.core;
import aegis.engine;

export namespace aegis::editor
{
/// @brief The ImGui name that identifies the window titled 'title'. ImGui identifies a window by the part of its label
///        from "###" on, so a lookup by name (FindWindowByName, the dock builder, the layout file) uses this one.
inline auto windowName(std::string_view title) -> std::string
{
    return std::format("###{}", title);
}

/// @brief The label of the window titled 'title': 'icon' (if any) and the title in capitals on its tab or title bar,
///        the window identified by windowName(title).
inline auto windowLabel(const char* icon, std::string_view title) -> std::string
{
    std::string caps{ title };
    std::ranges::transform(caps, caps.begin(), [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
    return icon ? std::format("{} {}{}", icon, caps, windowName(title)) : std::format("{}{}", caps, windowName(title));
}

/// @brief Where a panel's window sits. The editor begins and ends the window by it; the panel draws only its contents.
enum class Placement
{
    Dock,   // A window of its own, docked anywhere in the layout
    Column, // In the right column of the default layout (the Hierarchy, the Inspector), which folds into the side rail
    Drawer, // In the drawer over the bottom of the viewport, or docked in a bottom node of the layout while pinned
    Float,  // A window of its own that floats over the others; not opened by `--editor-panels all`
    Dialog, // A floating window centered as it appears, sized to its contents, with the panel's title and no saved layout
};

/// @brief The menu that lists a panel.
enum class Menu
{
    None,  // No menu: an action opens it (the dialogs)
    View,  // For the user
    Debug, // For the editor's developers
};

/// @brief A count on a panel's status-bar button (the console's warnings): its icon (one of theme::icons) and the
///        number, both in the color of 'level' (the warning's or the error's).
struct StatusBadge
{
    const char* icon;
    std::size_t count;
    log::Level level;
};

/// @brief One window of the editor: listed in its menu and drawn while open.
class Panel
{
public:
    Panel() = default;
    Panel(const Panel&) = delete;
    Panel(Panel&&) = delete;
    virtual ~Panel() = default;

    auto operator=(const Panel&) -> Panel& = delete;
    auto operator=(Panel&&) -> Panel& = delete;

    /// @brief The name in its menu, which is also its window's title.
    [[nodiscard]] virtual auto name() const -> std::string_view = 0;
    /// @brief Where its window sits; a docked window of its own by default.
    [[nodiscard]] virtual auto placement() const -> Placement { return Placement::Dock; }
    /// @brief The menu that lists it; the View menu by default.
    [[nodiscard]] virtual auto menu() const -> Menu { return Menu::View; }
    /// @brief For a dialog: its fixed width at 100% scale, at which its text wraps; 0 sizes it to its contents.
    [[nodiscard]] virtual auto dialogWidth() const -> float { return 0.0f; }
    /// @brief Its window's title; its name by default.
    [[nodiscard]] virtual auto title() const -> std::string_view { return name(); }
    /// @brief The icon its tab shows before the title (one of theme::icons); none for a dialog, which has its own title.
    [[nodiscard]] virtual auto icon() const -> const char* { return nullptr; }
    /// @brief Its window's label: the icon and the title in capitals, identified by windowName(title()).
    [[nodiscard]] auto label() const -> std::string { return windowLabel(icon(), title()); }
    /// @brief For a drawer panel: the id of the action that opens and closes it in the drawer, whose chord its menu item
    ///        shows; none (empty) by default.
    [[nodiscard]] virtual auto toggleAction() const -> std::string_view { return {}; }
    /// @brief For a drawer panel: whether the status bar has a button for it, its icon and name, lit while the drawer
    ///        shows it; the button's tooltip is its toggle action's.
    [[nodiscard]] virtual auto statusButton() const -> bool { return false; }
    /// @brief The counts its status-bar button shows after its name, each only while it isn't 0; none by default.
    [[nodiscard]] virtual auto statusBadges(const EditorContext& /*editor*/) const -> std::vector<StatusBadge> { return {}; }
    /// @brief A line its status-bar button's tooltip adds below the action's; none (empty) by default.
    [[nodiscard]] virtual auto statusTooltip(const EditorContext& /*editor*/) const -> std::string { return {}; }
    /// @brief Draws the panel's contents at the frame's safe point, inside the window the editor began for it. Panels
    ///        share state only through 'editor'.
    virtual void draw(EditorContext& editor, FrameContext& frame) = 0;

    [[nodiscard]] auto isOpen() const -> bool { return m_open; }
    void setOpen(bool open) { m_open = open; }

    /// @brief For a drawer panel: the width its first row leaves free at the right, where the pin button of a pinned
    ///        panel sits; 0 in the drawer, whose header holds the button.
    [[nodiscard]] auto trailingWidth() const -> float { return m_trailingWidth; }
    void setTrailingWidth(float width) { m_trailingWidth = width; }
    /// @brief The width of a first-row item that fills the rest of the row (SetNextItemWidth), short of trailingWidth.
    [[nodiscard]] auto fillWidth() const -> float { return m_trailingWidth > 0.0f ? -m_trailingWidth : -std::numeric_limits<float>::min(); }

private:
    bool m_open{ false };
    float m_trailingWidth{ 0.0f };
};
}
