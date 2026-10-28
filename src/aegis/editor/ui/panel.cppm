export module aegis.editor:panel;
import std;
import aegis.editor.core;
import aegis.engine;

export namespace aegis::editor
{
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
    /// @brief Draws the panel at the frame's safe point. Panels share state only through 'editor'.
    virtual void draw(EditorContext& editor, FrameContext& frame) = 0;

    [[nodiscard]] auto isOpen() const -> bool { return m_open; }
    void setOpen(bool open) { m_open = open; }

private:
    bool m_open{ false };
};
}
