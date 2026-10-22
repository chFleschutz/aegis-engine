module;

#ifdef _WIN32
// Win32 only to synthesize DPI and close messages for verifyWindow; other platforms skip those parts.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include <GLFW/glfw3.h>
#ifdef _WIN32
#include <GLFW/glfw3native.h>
#endif

module aegis.test;

import std;
import aegis.platform;

namespace aegis::test
{
// Checks the window's title, content scale and close veto. The scale changes and close requests are
// synthetic Win32 messages run through the real event loop, so no one has to move the window to
// another monitor or click its close button.
auto Harness::verifyWindow() -> std::expected<void, std::string>
{
    const std::string originalTitle{ m_window->title() };
    m_window->setTitle("aegis-test: window check");
    const std::string_view glfwTitle{ glfwGetWindowTitle(m_window->glfwWindow()) };
    const bool titleSet = m_window->title() == "aegis-test: window check" && glfwTitle == m_window->title();
    m_window->setTitle(originalTitle);
    if (!titleSet)
        return std::unexpected{ std::format("Window: title '{}' (GLFW '{}') after setTitle", m_window->title(), glfwTitle) };

    const float originalScale = m_window->contentScale();
    if (!(originalScale > 0.0f))
        return std::unexpected{ std::format("Window: content scale {} is not positive", originalScale) };

#ifndef _WIN32
    std::println("Window verified: title (content scale changes and close veto are only checked on Win32)");
    return {};
#else
    const auto hwnd = glfwGetWin32Window(m_window->glfwWindow());

    // WM_DPICHANGED carries the new DPI and a suggested rectangle; the current one keeps the window as is.
    std::vector<float> reported;
    m_window->setContentScaleHandler([&reported](float scale) { reported.push_back(scale); });
    const auto changeDpi = [this, hwnd](WORD dpi) {
        RECT rect{};
        GetWindowRect(hwnd, &rect);
        SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&rect));
        return m_window->contentScale();
    };
    const float scale125 = changeDpi(120);
    const float scale150 = changeDpi(144);
    const auto originalDpi = static_cast<WORD>(std::lround(originalScale * USER_DEFAULT_SCREEN_DPI));
    const float restoredScale = changeDpi(originalDpi);
    m_window->setContentScaleHandler({});
    m_window->update(); // The resizes the messages may have caused

    // A close request (the close button sends WM_CLOSE) that the handler refuses, then one it accepts.
    std::uint32_t asked{ 0 };
    bool allow{ false };
    m_window->setCloseHandler([&asked, &allow] { ++asked; return allow; });
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
    m_window->update();
    const bool vetoed = asked == 1 && !m_window->shouldClose();
    allow = true;
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
    m_window->update();
    const bool accepted = asked == 2 && m_window->shouldClose();
    glfwSetWindowShouldClose(m_window->glfwWindow(), GLFW_FALSE);
    allow = false;
    m_window->close(); // The app's own close, after a prompt: not put to the handler
    const bool closed = asked == 2 && m_window->shouldClose();
    m_window->setCloseHandler({});
    glfwSetWindowShouldClose(m_window->glfwWindow(), GLFW_FALSE); // Keep running the frames

    if (scale125 != 1.25f || scale150 != 1.5f || restoredScale != originalScale)
        return std::unexpected{ std::format("Window: content scale {} at 120 DPI, {} at 144 DPI, {} restored (was {})",
            scale125, scale150, restoredScale, originalScale) };
    if (reported != std::vector{ 1.25f, 1.5f, originalScale })
        return std::unexpected{ std::format("Window: content scale handler got {}, expected [1.25, 1.5, {}]", reported, originalScale) };
    if (!vetoed || !accepted || !closed)
        return std::unexpected{
            std::format("Window: close handler asked {} times (vetoed close kept the window {}, accepted closed it {}, close() closed it {})",
                asked, vetoed, accepted, closed)
        };

    std::println("Window verified: title, content scale and its change handler (125%, 150%), close veto");
    return {};
#endif
}
}
