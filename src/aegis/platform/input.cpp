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

    m_window.m_keyHandler = [this](int key, int action, int mods) { onKey(key, action, mods); };
}

Input::~Input()
{
    m_window.m_keyHandler = nullptr;
}

auto Input::keyPressed(Key key) const -> bool
{
    return !m_keyboardCaptured && glfwGetKey(m_window.glfwWindow(), static_cast<int>(key)) == GLFW_PRESS;
}

auto Input::mouseButtonPressed(MouseButton button) const -> bool
{
    return !m_mouseCaptured && glfwGetMouseButton(m_window.glfwWindow(), static_cast<int>(button)) == GLFW_PRESS;
}

auto Input::cursorPosition() const -> CursorPosition
{
    CursorPosition position;
    glfwGetCursorPos(m_window.glfwWindow(), &position.x, &position.y);
    return position;
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

auto Input::bind(Key key, std::function<void()> callback, KeyAction action, Modifiers modifiers) -> void
{
    m_bindings[key].emplace_back(std::move(callback), action, modifiers);
}

auto Input::onKey(int key, int action, int mods) -> void
{
    const auto bindings = m_bindings.find(static_cast<Key>(key));
    if (bindings == m_bindings.end())
        return;

    for (const auto& binding : bindings->second)
    {
        if (binding.action == static_cast<KeyAction>(action) && binding.modifiers == static_cast<Modifiers>(mods))
            binding.callback();
    }
}
}
