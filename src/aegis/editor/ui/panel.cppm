export module aegis.editor:panel;
import std;
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

/// @brief One window of the editor, or a group of them: listed in the View or the Debug menu and drawn while open.
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
    /// @brief Whether it is a tool for the editor's developers (the Debug menu) rather than for the user (the View menu).
    [[nodiscard]] virtual auto debug() const -> bool { return false; }
    /// @brief Whether it has its place in the default layout; one that floats over the other panels (the ImGui demo) is
    ///        not opened by `--editor-panels all`.
    [[nodiscard]] virtual auto docked() const -> bool { return true; }
    /// @brief Whether it opens in the drawer over the bottom of the viewport, or pinned in a bottom node of the layout,
    ///        instead of a window of its own (Content, Console, the debug panels). The shell begins and ends its window;
    ///        draw draws only its contents.
    [[nodiscard]] virtual auto drawer() const -> bool { return false; }
    /// @brief Whether it sits in the right column of the default layout (the Hierarchy, the Inspector). The shell begins
    ///        and ends its window, titled title(); draw draws only its contents.
    [[nodiscard]] virtual auto column() const -> bool { return false; }
    /// @brief Its window's title; its name by default.
    [[nodiscard]] virtual auto title() const -> std::string_view { return name(); }
    /// @brief The icon its tab shows before the title (one of theme::icons); none for a dialog, which has its own title.
    [[nodiscard]] virtual auto icon() const -> const char* { return nullptr; }
    /// @brief Its window's label: the icon and the title in capitals, identified by windowName(title()).
    [[nodiscard]] auto label() const -> std::string { return windowLabel(icon(), title()); }
    /// @brief Draws the panel at the frame's safe point. Panels share state only through 'editor'.
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
