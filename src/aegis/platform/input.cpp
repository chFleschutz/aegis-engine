module;
#include "GLFW/glfw3.h"

module aegis.platform;

import std;

namespace aegis::platform
{
Input::Input(Window& window) :
    m_window{ window }
{
    // Unaccelerated motion while the cursor is disabled, which is what camera control wants.
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(m_window.glfwWindow(), GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

    double x = 0.0, y = 0.0;
    glfwGetCursorPos(m_window.glfwWindow(), &x, &y);
    m_state.placeCursor({ x, y });

    m_window.m_keyHandler = [this](int key, int action, int mods) { onKey(key, action, mods); };
}

Input::~Input()
{
    double x = 0.0, y = 0.0;
    glfwGetCursorPos(m_window.glfwWindow(), &x, &y);
    m_state.placeCursor({ x, y });

    m_window.m_keyHandler = nullptr;
}

auto Input::update() -> void
{
    m_state.beginFrame();
    for (const auto& event : m_window.takeEvents())
        m_state.apply(event);
    m_state.apply(pollGamepad());
}

auto Input::pollGamepad() -> GamepadEvent
{
    // The first connected gamepad; a joystick without a gamepad mapping does not count.
    for (int joystick = GLFW_JOYSTICK_1; joystick <= GLFW_JOYSTICK_LAST; ++joystick)
    {
        GLFWgamepadstate state{};
        if (!glfwJoystickIsGamepad(joystick) || !glfwGetGamepadState(joystick, &state))
            continue;

        GamepadEvent event{ .connected = true };
        for (std::size_t i = 0; i < event.buttons.size(); ++i)
            event.buttons[i] = state.buttons[i] == GLFW_PRESS;
        for (std::size_t i = 0; i < event.axes.size(); ++i)
            event.axes[i] = state.axes[i];
        // GLFW's triggers rest at -1; InputState's at 0
        for (const auto trigger : { GLFW_GAMEPAD_AXIS_LEFT_TRIGGER, GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER })
            event.axes[trigger] = (state.axes[trigger] + 1.0f) * 0.5f;
        return event;
    }
    return {};
}

auto Input::setCursorMode(CursorMode mode) -> void
{
    const auto value = [mode] {
        switch (mode)
        {
        case CursorMode::Normal: return GLFW_CURSOR_NORMAL;
        case CursorMode::Hidden: return GLFW_CURSOR_HIDDEN;
        case CursorMode::Disabled: return GLFW_CURSOR_DISABLED;
        }
        std::unreachable();
    }();
    glfwSetInputMode(m_window.glfwWindow(), GLFW_CURSOR, value);
}

auto Input::bind(Key key, std::function<void()> callback, KeyAction action, Modifiers modifiers) -> BindingId
{
    return m_bindings.bind(key, std::move(callback), action, modifiers);
}

auto Input::unbind(BindingId id) -> bool
{
    return m_bindings.unbind(id);
}

auto Input::onKey(int key, int action, int mods) -> void
{
    m_bindings.dispatch(static_cast<Key>(key), static_cast<KeyAction>(action), static_cast<Modifiers>(mods));
}
}
