module;
#include <GLFW/glfw3.h>

export module aegis.platform:input_state;
import std;
import :key;
import :input_map;

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

inline constexpr std::size_t GAMEPAD_BUTTON_COUNT = GLFW_GAMEPAD_BUTTON_LAST + 1;
inline constexpr std::size_t GAMEPAD_AXIS_COUNT = GLFW_GAMEPAD_AXIS_LAST + 1;

/// @brief The whole state of the gamepad, polled once a frame (gamepads send no events). Indexed by GamepadButton and
///        GamepadAxis; the axes in InputState's ranges (triggers from 0 to 1, see GamepadAxis). A gamepad that is not
///        connected has nothing down and every axis at 0.
struct GamepadEvent
{
    bool connected{ false };
    std::array<bool, GAMEPAD_BUTTON_COUNT> buttons{};
    std::array<float, GAMEPAD_AXIS_COUNT> axes{};
};

using InputEvent = std::variant<KeyEvent, MouseButtonEvent, CursorEvent, ScrollEvent, GamepadEvent>;

/// @brief An input action this frame: down while any of its inputs is; pressed in the frame it went down, released in
///        the frame it went up (both for an input tapped within one frame). Another input going down while the
///        action is already down is no new press.
class ActionState
{
public:
    ActionState() = default;
    ActionState(bool down, bool pressed, bool released) :
        m_down{ down },
        m_pressed{ pressed },
        m_released{ released }
    {
    }

    [[nodiscard]] auto down() const -> bool { return m_down; }
    [[nodiscard]] auto pressed() const -> bool { return m_pressed; }
    [[nodiscard]] auto released() const -> bool { return m_released; }

private:
    bool m_down{ false };
    bool m_pressed{ false };
    bool m_released{ false };
};

/// @brief What the keyboard, mouse and gamepad did, as a plain value built from events: no window, no GLFW.
///
/// Call beginFrame once per frame, then apply the frame's events. Keys and buttons are "down" while held;
/// "pressed" and "released" are true only for the frame in which they happened. While captured (e.g. by
/// a UI that has the focus) the keyboard or mouse queries report nothing, so game code sees no input; the
/// cursor position stays readable. The raw state is still tracked, so releasing the capture
/// does not lose a key that is held. The gamepad counts as captured with the keyboard (a UI navigates with it).
///
/// The input actions and axes of its InputMap are read by name (action, axis), computed from the same state and
/// capture: an input of a captured device counts as up.
class InputState
{
public:
    /// @brief Starts a frame: forgets what only lasted one frame (pressed, released, cursor delta, scroll).
    auto beginFrame() -> void
    {
        m_keysDownBefore = m_keysDown;
        m_buttonsDownBefore = m_buttonsDown;
        m_gamepadBefore = m_gamepad;
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

    /// @brief The input actions and axes that action and axis read.
    auto setMap(InputMap map) -> void { m_map = std::move(map); }
    [[nodiscard]] auto map() const -> const InputMap& { return m_map; }

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

    /// @brief True while a gamepad is connected, captured or not.
    [[nodiscard]] auto gamepadConnected() const -> bool { return m_gamepad.connected; }
    [[nodiscard]] auto gamepadDown(GamepadButton button) const -> bool { return !m_keyboardCaptured && gamepadBit(m_gamepad, button); }
    [[nodiscard]] auto gamepadPressed(GamepadButton button) const -> bool
    {
        return !m_keyboardCaptured && gamepadBit(m_gamepad, button) && !gamepadBit(m_gamepadBefore, button);
    }
    [[nodiscard]] auto gamepadReleased(GamepadButton button) const -> bool
    {
        return !m_keyboardCaptured && !gamepadBit(m_gamepad, button) && gamepadBit(m_gamepadBefore, button);
    }
    /// @brief The axis's value, without the map's dead zone (0 while the keyboard is captured)
    [[nodiscard]] auto gamepadAxis(GamepadAxis axis) const -> float { return m_keyboardCaptured ? 0.0f : gamepadValue(m_gamepad, axis); }

    /// @brief The input action 'name' of the map. An unknown name is an action that is never down, and logs a warning
    ///        the first time.
    [[nodiscard]] auto action(std::string_view name) const -> ActionState;
    /// @brief The value of the input axis 'name' of the map, from -1 to 1. An unknown name reads 0, and logs a warning
    ///        the first time.
    [[nodiscard]] auto axis(std::string_view name) const -> float;

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

    /// @brief One input as an action reads it: down now and at the frame's start, and whether it went down or up this
    ///        frame. All false while its device is captured.
    struct SourceState
    {
        bool down{ false };
        bool downBefore{ false };
        bool pressed{ false };
        bool released{ false };
    };

    [[nodiscard]] auto sourceState(const InputSource& source) const -> SourceState;
    [[nodiscard]] auto bindingValue(const AxisBinding& binding) const -> float;
    auto warnUnknown(std::string_view kind, std::string_view name) const -> void;

    [[nodiscard]] static auto gamepadBit(const GamepadEvent& gamepad, GamepadButton button) -> bool
    {
        const auto i = index<GAMEPAD_BUTTON_COUNT>(static_cast<int>(button));
        return i && gamepad.buttons[*i];
    }

    [[nodiscard]] static auto gamepadValue(const GamepadEvent& gamepad, GamepadAxis axis) -> float
    {
        const auto i = index<GAMEPAD_AXIS_COUNT>(static_cast<int>(axis));
        return i ? gamepad.axes[*i] : 0.0f;
    }

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

    auto applyEvent(const GamepadEvent& e) -> void
    {
        m_gamepad = e.connected ? e : GamepadEvent{};
    }

    std::bitset<KEY_COUNT> m_keysDown;
    std::bitset<KEY_COUNT> m_keysDownBefore; // As the frame started, for the input actions' edges
    std::bitset<KEY_COUNT> m_keysPressed;
    std::bitset<KEY_COUNT> m_keysReleased;
    std::bitset<BUTTON_COUNT> m_buttonsDown;
    std::bitset<BUTTON_COUNT> m_buttonsDownBefore;
    std::bitset<BUTTON_COUNT> m_buttonsPressed;
    std::bitset<BUTTON_COUNT> m_buttonsReleased;
    GamepadEvent m_gamepad;
    GamepadEvent m_gamepadBefore;
    InputMap m_map;
    mutable std::set<std::string, std::less<>> m_warnedNames; // Unknown names already warned about, as "kind:name"
    CursorPosition m_cursor;
    CursorPosition m_cursorDelta;
    CursorPosition m_scroll;
    Modifiers m_modifiers{ Modifiers::None };
    bool m_hasCursor{ false };
    bool m_keyboardCaptured{ false };
    bool m_mouseCaptured{ false };
};
}
