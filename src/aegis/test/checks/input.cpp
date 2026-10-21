module;

#ifdef _WIN32
// Win32 only to synthesize input events for verifyInput; other platforms skip that check.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#endif

module aegis.test;

import std;
import aegis.platform;

namespace aegis
{
// Posts synthetic Win32 key and mouse messages to the window and lets the real event loop process
// them, so polling and key bindings are checked end to end without anyone at the keyboard.
// Assumes no modifier key is physically held during the run: GLFW reads modifiers from the keyboard.
// Windows only: X11 and Wayland have no equivalent that reaches GLFW's event loop as reliably.
auto Engine::verifyInput() -> std::expected<void, std::string>
{
#ifndef _WIN32
    std::println("Input check skipped: synthetic input is only implemented for Win32");
    return {};
#else
    const auto hwnd = glfwGetWin32Window(m_window->glfwWindow());
    const auto postKey = [hwnd](UINT message, WPARAM virtualKey) {
        // GLFW identifies the key by the scancode in bits 16-23, then sets the state from the message.
        const auto scancode = MapVirtualKeyW(static_cast<UINT>(virtualKey), MAPVK_VK_TO_VSC);
        auto lParam = static_cast<LPARAM>(1 | (scancode << 16));
        if (message == WM_KEYUP)
            lParam |= (LPARAM{ 1 } << 30) | (LPARAM{ 1 } << 31);
        PostMessageW(hwnd, message, virtualKey, lParam);
    };

    // Shared, not 'this': the bindings outlive this call and the Engine is moved after create().
    const auto presses = std::make_shared<std::uint32_t>(0);
    m_input->bind(platform::Key::K, [presses] { ++*presses; });
    m_input->bind(platform::Key::K, [presses] { ++*presses; }, platform::KeyAction::Press,
        platform::Modifiers::Control); // Must not fire: Control is not held

    // Each step is one frame: window events in, then the snapshot built from them.
    const auto frame = [this] {
        m_window->update();
        m_input->update();
        return &m_input->state();
    };

    postKey(WM_KEYDOWN, 'K');
    const bool keyDown = frame()->keyDown(platform::Key::K) && m_input->state().keyPressed(platform::Key::K);
    const auto pressesAfterDown = *presses;

    const bool keyHeld = frame()->keyDown(platform::Key::K) && !m_input->state().keyPressed(platform::Key::K);

    postKey(WM_KEYUP, 'K');
    const bool keyUp = !frame()->keyDown(platform::Key::K) && m_input->state().keyReleased(platform::Key::K);

    PostMessageW(hwnd, WM_RBUTTONDOWN, MK_RBUTTON, 0);
    const bool buttonDown = frame()->mouseDown(platform::MouseButton::Right);

    PostMessageW(hwnd, WM_RBUTTONUP, 0, 0);
    const bool buttonUp = !frame()->mouseDown(platform::MouseButton::Right);

    PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(10, 10));
    frame();
    PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(30, 15));
    const auto cursor = frame()->cursor();
    const auto delta = m_input->state().cursorDelta();
    const bool cursorMoved = cursor.x == 30.0 && cursor.y == 15.0 && delta.x == 20.0 && delta.y == 5.0;

    PostMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
    const bool scrolled = frame()->scroll().y == 1.0;
    const bool scrollCleared = frame()->scroll().y == 0.0;

    m_input->setCaptured(true, true);
    const bool captured = !m_input->state().keyDown(platform::Key::K) && m_input->state().cursorDelta().x == 0.0;
    m_input->setCaptured(false, false);

    if (!keyDown || !keyHeld || !keyUp)
        return std::unexpected{ std::format("Input: key state wrong (down {}, held {}, released {})", keyDown, keyHeld, keyUp) };
    if (pressesAfterDown != 1 || *presses != 1)
        return std::unexpected{
            std::format("Input: expected 1 binding call on press only, got {} then {}", pressesAfterDown,
                *presses)
        };
    if (!buttonDown || !buttonUp)
        return std::unexpected{
            std::format("Input: mouse button state wrong (down {}, released {})", buttonDown, buttonUp)
        };
    if (!cursorMoved)
        return std::unexpected{ std::format("Input: cursor ({}, {}) delta ({}, {}), expected (30, 15) and (20, 5)",
            cursor.x, cursor.y, delta.x, delta.y) };
    if (!scrolled || !scrollCleared)
        return std::unexpected{ std::format("Input: scroll wrong (seen {}, cleared next frame {})", scrolled, scrollCleared) };
    if (!captured)
        return std::unexpected{ "Input: a captured state still reports input" };

    std::println("Input verified: InputState snapshot (keys, mouse button, cursor delta, scroll, capture) and key binding (press only, exact modifiers)");
    return {};
#endif
}
}
