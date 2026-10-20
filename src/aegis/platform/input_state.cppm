module;
#include <GLFW/glfw3.h>

export module aegis.platform:input_state;
import std;
import :key;

export namespace aegis::platform
{
/// @brief In screen coordinates, relative to the window's top-left corner.
struct CursorPosition
{
    double x{ 0.0 };
    double y{ 0.0 };
};

struct KeyEvent
{
    Key key{ Key::Unknown };
    KeyAction action{ KeyAction::Release };
    Modifiers modifiers{ Modifiers::None };
};

struct MouseButtonEvent
{
    MouseButton button{ MouseButton::Left };
    KeyAction action{ KeyAction::Release };
    Modifiers modifiers{ Modifiers::None };
};

/// @brief The cursor moved to a position, in the units of CursorPosition.
struct CursorEvent
{
    double x{ 0.0 };
    double y{ 0.0 };
};

struct ScrollEvent
{
    double x{ 0.0 };
    double y{ 0.0 };
};

using InputEvent = std::variant<KeyEvent, MouseButtonEvent, CursorEvent, ScrollEvent>;

/// @brief What the keyboard and mouse did, as a plain value built from events: no window, no GLFW.
///
/// Call beginFrame once per frame, then apply the frame's events. Keys and buttons are "down" while held;
/// "pressed" and "released" are true only for the frame in which they happened. While captured (e.g. by
/// a UI that has the focus) the keyboard or mouse queries report nothing, so game code sees no input; the
/// cursor position stays readable. The raw state is still tracked, so releasing the capture
/// does not lose a key that is held.
class InputState
{
public:
    /// @brief Starts a frame: forgets what only lasted one frame (pressed, released, cursor delta, scroll).
    auto beginFrame() -> void
    {
        m_keysPressed.reset();
        m_keysReleased.reset();
        m_buttonsPressed.reset();
        m_buttonsReleased.reset();
        m_cursorDelta = {};
        m_scroll = {};
    }

    auto apply(const InputEvent& event) -> void
    {
        std::visit([this](const auto& e) { applyEvent(e); }, event);
    }

    /// @brief Puts the cursor at 'position' without a delta (a start position, not a movement)
    auto placeCursor(CursorPosition position) -> void
    {
        m_cursor = position;
        m_hasCursor = true;
    }

    auto setCaptured(bool keyboard, bool mouse) -> void
    {
        m_keyboardCaptured = keyboard;
        m_mouseCaptured = mouse;
    }

    [[nodiscard]] auto keyboardCaptured() const -> bool { return m_keyboardCaptured; }
    [[nodiscard]] auto mouseCaptured() const -> bool { return m_mouseCaptured; }

    /// @brief True while 'key' is held. Key::Unknown and keys the backend does not know are never down.
    [[nodiscard]] auto keyDown(Key key) const -> bool { return keyboardBit(m_keysDown, key); }
    /// @brief True in the frame 'key' went down (a repeat does not count)
    [[nodiscard]] auto keyPressed(Key key) const -> bool { return keyboardBit(m_keysPressed, key); }
    /// @brief True in the frame 'key' went up
    [[nodiscard]] auto keyReleased(Key key) const -> bool { return keyboardBit(m_keysReleased, key); }

    [[nodiscard]] auto mouseDown(MouseButton button) const -> bool { return mouseBit(m_buttonsDown, button); }
    [[nodiscard]] auto mousePressed(MouseButton button) const -> bool { return mouseBit(m_buttonsPressed, button); }
    [[nodiscard]] auto mouseReleased(MouseButton button) const -> bool { return mouseBit(m_buttonsReleased, button); }

    /// @brief Where the cursor is. Not affected by the capture.
    [[nodiscard]] auto cursor() const -> CursorPosition { return m_cursor; }
    /// @brief How far the cursor moved this frame (zero while the mouse is captured)
    [[nodiscard]] auto cursorDelta() const -> CursorPosition { return m_mouseCaptured ? CursorPosition{} : m_cursorDelta; }
    /// @brief How far the wheel scrolled this frame (zero while the mouse is captured)
    [[nodiscard]] auto scroll() const -> CursorPosition { return m_mouseCaptured ? CursorPosition{} : m_scroll; }
    /// @brief The modifiers of the last key or button event (zero while the keyboard is captured)
    [[nodiscard]] auto modifiers() const -> Modifiers { return m_keyboardCaptured ? Modifiers::None : m_modifiers; }

private:
    static constexpr std::size_t KEY_COUNT = GLFW_KEY_LAST + 1;
    static constexpr std::size_t BUTTON_COUNT = GLFW_MOUSE_BUTTON_LAST + 1;

    template<std::size_t N>
    [[nodiscard]] static auto index(int value) -> std::optional<std::size_t>
    {
        if (value < 0 || static_cast<std::size_t>(value) >= N)
            return std::nullopt;
        return static_cast<std::size_t>(value);
    }

    [[nodiscard]] auto keyboardBit(const std::bitset<KEY_COUNT>& bits, Key key) const -> bool
    {
        const auto i = index<KEY_COUNT>(static_cast<int>(key));
        return !m_keyboardCaptured && i && bits[*i];
    }

    [[nodiscard]] auto mouseBit(const std::bitset<BUTTON_COUNT>& bits, MouseButton button) const -> bool
    {
        const auto i = index<BUTTON_COUNT>(static_cast<int>(button));
        return !m_mouseCaptured && i && bits[*i];
    }

    auto applyEvent(const KeyEvent& e) -> void
    {
        m_modifiers = e.modifiers;
        const auto i = index<KEY_COUNT>(static_cast<int>(e.key));
        if (!i)
            return;
        if (e.action == KeyAction::Press)
        {
            m_keysDown.set(*i);
            m_keysPressed.set(*i);
        }
        else if (e.action == KeyAction::Release)
        {
            m_keysDown.reset(*i);
            m_keysReleased.set(*i);
        }
    }

    auto applyEvent(const MouseButtonEvent& e) -> void
    {
        m_modifiers = e.modifiers;
        const auto i = index<BUTTON_COUNT>(static_cast<int>(e.button));
        if (!i)
            return;
        if (e.action == KeyAction::Press)
        {
            m_buttonsDown.set(*i);
            m_buttonsPressed.set(*i);
        }
        else if (e.action == KeyAction::Release)
        {
            m_buttonsDown.reset(*i);
            m_buttonsReleased.set(*i);
        }
    }

    auto applyEvent(const CursorEvent& e) -> void
    {
        if (m_hasCursor)
        {
            m_cursorDelta.x += e.x - m_cursor.x;
            m_cursorDelta.y += e.y - m_cursor.y;
        }
        m_cursor = { e.x, e.y };
        m_hasCursor = true; // The first position is a start, not a movement
    }

    auto applyEvent(const ScrollEvent& e) -> void
    {
        m_scroll.x += e.x;
        m_scroll.y += e.y;
    }

    std::bitset<KEY_COUNT> m_keysDown;
    std::bitset<KEY_COUNT> m_keysPressed;
    std::bitset<KEY_COUNT> m_keysReleased;
    std::bitset<BUTTON_COUNT> m_buttonsDown;
    std::bitset<BUTTON_COUNT> m_buttonsPressed;
    std::bitset<BUTTON_COUNT> m_buttonsReleased;
    CursorPosition m_cursor;
    CursorPosition m_cursorDelta;
    CursorPosition m_scroll;
    Modifiers m_modifiers{ Modifiers::None };
    bool m_hasCursor{ false };
    bool m_keyboardCaptured{ false };
    bool m_mouseCaptured{ false };
};
}
