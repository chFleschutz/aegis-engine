export module aegis.editor:panel;
import std;
import aegis.editor.core;
import aegis.engine;

export namespace aegis::editor
{
/// @brief One window of the editor, or a group of them: listed in the View menu and drawn while open.
class Panel
{
public:
    Panel() = default;
    Panel(const Panel&) = delete;
    Panel(Panel&&) = delete;
    virtual ~Panel() = default;

    auto operator=(const Panel&) -> Panel& = delete;
    auto operator=(Panel&&) -> Panel& = delete;

    /// @brief The name in the View menu.
    [[nodiscard]] virtual auto name() const -> std::string_view = 0;
    /// @brief Whether it is a tool for the editor's developers (the Debug menu) rather than for the user (the View menu).
    [[nodiscard]] virtual auto debug() const -> bool { return false; }
    /// @brief Draws the panel at the frame's safe point. Panels share state only through 'editor'.
    virtual void draw(EditorContext& editor, FrameContext& frame) = 0;

    [[nodiscard]] auto isOpen() const -> bool { return m_open; }
    void setOpen(bool open) { m_open = open; }

private:
    bool m_open{ false };
};
}
