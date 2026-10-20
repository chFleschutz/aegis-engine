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
