module;
#include <GLFW/glfw3.h>

export module aegis.platform:input;
import std;
import :key;
import :window;

export namespace aegis::platform
{
enum class CursorMode
{
    Normal,
    Hidden,   // Invisible over the window, but moves freely
    Disabled, // Invisible and captured: unbounded relative motion, e.g. for camera control
};

/// @brief In screen coordinates, relative to the window's top-left corner.
struct CursorPosition
{
    double x{ 0.0 };
    double y{ 0.0 };
};

class Input
{
public:
    /// @brief Receives 'window's key events; the window must outlive this Input and must not move.
    explicit Input(Window& window);
    Input(const Input&) = delete;
    Input(Input&&) = delete;
    ~Input();

    auto operator=(const Input&) -> Input& = delete;
    auto operator=(Input&&) -> Input& = delete;

    [[nodiscard]] auto keyPressed(Key key) const -> bool;
    [[nodiscard]] auto mouseButtonPressed(MouseButton button) const -> bool;
    [[nodiscard]] auto cursorPosition() const -> CursorPosition;

    auto setCursorMode(CursorMode mode) -> void;

    /// @brief While captured (e.g. by a UI that has the focus), keyPressed and mouseButtonPressed report
    ///        nothing pressed. Bindings and the cursor position are not affected.
    auto setCaptured(bool keyboard, bool mouse) -> void
    {
        m_keyboardCaptured = keyboard;
        m_mouseCaptured = mouse;
    }

    /// @brief Calls 'callback' whenever 'key' sees 'action' while exactly 'modifiers' are held.
    /// @note Callbacks run inside Window::update, which processes the events.
    auto bind(Key key, std::function<void()> callback, KeyAction action = KeyAction::Press,
        Modifiers modifiers = Modifiers::None) -> void;

private:
    struct Binding
    {
        std::function<void()> callback;
        KeyAction action;
        Modifiers modifiers;
    };

    auto onKey(int key, int action, int mods) -> void;

    Window& m_window;
    std::unordered_map<Key, std::vector<Binding>> m_bindings;
    bool m_keyboardCaptured{ false };
    bool m_mouseCaptured{ false };
};
}
