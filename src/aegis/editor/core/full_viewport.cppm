export module aegis.editor.core:full_viewport;
import std;

export namespace aegis::editor
{
/// @brief Whether the viewport has the whole window (full viewport: no menu bar, status bar or panels), and whether
///        Play entered it. The user toggles it (F11, the menu bar's button, the View menu); Play enters it when the
///        "Full viewport on play" preference is on, and Stop leaves it only if Play entered it and the user didn't
///        toggle it since.
class FullViewport
{
public:
    [[nodiscard]] auto active() const -> bool { return m_active; }
    /// @brief Whether Play entered it, so Stop leaves it.
    [[nodiscard]] auto enteredByPlay() const -> bool { return m_enteredByPlay; }

    /// @brief The user enters or leaves it; Stop then leaves it as it is.
    void toggle();
    /// @brief The user leaves it (Esc, a rename that needs the inspector); Stop then leaves it as it is.
    void leave();
    /// @brief Play started: enters it when 'onPlay' (the preference) is on and it isn't active yet.
    void play(bool onPlay);
    /// @brief Play stopped: leaves it if Play entered it.
    void stop();

private:
    bool m_active{ false };
    bool m_enteredByPlay{ false };
};
}
