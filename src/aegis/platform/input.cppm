module;
#include <GLFW/glfw3.h>

export module aegis.platform:input;
import std;
import :key;
import :input_state;
import :key_bindings;
import :window;

export namespace aegis::platform
{
enum class CursorMode
{
    Normal,
    Hidden,   // Invisible over the window, but moves freely
    Disabled, // Invisible and captured: unbounded relative motion, e.g. for camera control
};

class Input
{
public:
    /// @brief Receives 'window's events; the window must outlive this Input and must not move.
    explicit Input(Window& window);
    Input(const Input&) = delete;
    Input(Input&&) = delete;
    ~Input();

    auto operator=(const Input&) -> Input& = delete;
    auto operator=(Input&&) -> Input& = delete;

    /// @brief Builds this frame's InputState: forgets the last frame's, then applies the events the window
    ///        collected since. Call once per frame, after Window::update.
    auto update() -> void;

    /// @brief The state as of the last update. Stays valid, and changes in place, for the life of this Input.
    [[nodiscard]] auto state() const -> const InputState& { return m_state; }

    auto setCursorMode(CursorMode mode) -> void;

    /// @brief While captured (e.g. by a UI that has the focus), the state reports no keyboard or mouse input
    ///        (see InputState). Bindings and the cursor position are not affected.
    auto setCaptured(bool keyboard, bool mouse) -> void { m_state.setCaptured(keyboard, mouse); }

    /// @brief Calls 'callback' whenever 'key' sees 'action' while exactly 'modifiers' are held; see KeyBindings.
    /// @return The id to unbind with. Dropping it keeps the binding for the life of this Input.
    /// @note Callbacks run inside Window::update, which processes the events.
    auto bind(Key key, std::function<void()> callback, KeyAction action = KeyAction::Press,
        Modifiers modifiers = Modifiers::None) -> BindingId;

    auto unbind(BindingId id) -> bool;

private:
    auto onKey(int key, int action, int mods) -> void;

    Window& m_window;
    KeyBindings m_bindings;
    InputState m_state;
};
}
