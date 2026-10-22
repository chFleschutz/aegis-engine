module;
#include "GLFW/glfw3.h"

module aegis.platform;

import std;

namespace aegis::platform
{
namespace
{
// GLFW is process-wide state: the first window initializes it, the last one terminates it.
detail::InitRefCount s_glfwUsers;
detail::GlfwErrorCapture s_glfwErrors;

auto onGlfwError(int code, const char* description) -> void
{
    s_glfwErrors.record(code, description ? description : "");
}

auto initGlfw() -> bool
{
    glfwSetErrorCallback(onGlfwError); // Before glfwInit: that is where "no display" is reported
    return glfwInit() == GLFW_TRUE;
}
}

auto Window::create(const Desc& desc) -> std::expected<Window, Error>
{
    (void)s_glfwErrors.take(); // Forget anything older, it is not why this call fails
    if (!s_glfwUsers.acquire(initGlfw))
        return std::unexpected{ detail::initError(s_glfwErrors.take()) };

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    const std::string title{ desc.title }; // GLFW reads a null-terminated string
    GLFWwindow* handle = glfwCreateWindow(static_cast<int>(desc.width),
        static_cast<int>(desc.height),
        title.c_str(),
        nullptr,
        nullptr);
    if (!handle)
    {
        s_glfwUsers.release(glfwTerminate);
        return std::unexpected{ detail::windowError(s_glfwErrors.take()) };
    }

    return Window{ handle, desc };
}

Window::Window(GLFWwindow* window, const Desc& desc) :
    m_window{ window },
    m_width{ desc.width },
    m_height{ desc.height },
    m_title{ desc.title }
{
    glfwGetWindowContentScale(m_window, &m_contentScale, nullptr);

    glfwSetWindowUserPointer(m_window, this);
    // Framebuffer size, not window size: it is what a swapchain must match, and update() reads it too.
    glfwSetFramebufferSizeCallback(m_window, onFramebufferResize);
    glfwSetKeyCallback(m_window, onKey);
    glfwSetMouseButtonCallback(m_window, onMouseButton);
    glfwSetCursorPosCallback(m_window, onCursorPos);
    glfwSetScrollCallback(m_window, onScroll);
    glfwSetWindowContentScaleCallback(m_window, onContentScale);
    glfwSetWindowCloseCallback(m_window, onClose);
}

Window::Window(Window&& other) noexcept :
    m_window{ std::exchange(other.m_window, nullptr) },
    m_width{ other.m_width },
    m_height{ other.m_height },
    m_wasResized{ other.m_wasResized },
    m_contentScale{ other.m_contentScale },
    m_title{ std::move(other.m_title) },
    m_keyHandler{ std::move(other.m_keyHandler) },
    m_contentScaleHandler{ std::move(other.m_contentScaleHandler) },
    m_closeHandler{ std::move(other.m_closeHandler) },
    m_events{ std::move(other.m_events) }
{
    if (m_window)
        glfwSetWindowUserPointer(m_window, this);
}

Window::~Window()
{
    if (m_window)
    {
        glfwDestroyWindow(m_window);
        s_glfwUsers.release(glfwTerminate);
    }
}

auto Window::shouldClose() const -> bool
{
    return glfwWindowShouldClose(m_window);
}

auto Window::setTitle(std::string_view title) -> void
{
    m_title = title; // Also the null-terminated string GLFW reads
    glfwSetWindowTitle(m_window, m_title.c_str());
}

auto Window::close() -> void
{
    glfwSetWindowShouldClose(m_window, GLFW_TRUE);
}

auto Window::update() -> void
{
    glfwPollEvents();

    int width, height;
    glfwGetFramebufferSize(m_window, &width, &height);
    m_width = static_cast<std::uint32_t>(width);
    m_height = static_cast<std::uint32_t>(height);
}

auto Window::waitEvents() -> void
{
    glfwWaitEvents();
}

auto Window::onFramebufferResize(GLFWwindow* glfwWindow, int newWidth, int newHeight) -> void
{
    const auto window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
    window->m_width = static_cast<std::uint32_t>(newWidth);
    window->m_height = static_cast<std::uint32_t>(newHeight);
    window->m_wasResized = true;
}

auto Window::onKey(GLFWwindow* glfwWindow, int key, int /*scancode*/, int action, int mods) -> void
{
    const auto window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
    window->m_events.emplace_back(KeyEvent{ static_cast<Key>(key), static_cast<KeyAction>(action), static_cast<Modifiers>(mods) });
    if (window->m_keyHandler)
        window->m_keyHandler(key, action, mods);
}

auto Window::onMouseButton(GLFWwindow* glfwWindow, int button, int action, int mods) -> void
{
    const auto window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
    window->m_events.emplace_back(
        MouseButtonEvent{ static_cast<MouseButton>(button), static_cast<KeyAction>(action), static_cast<Modifiers>(mods) });
}

auto Window::onCursorPos(GLFWwindow* glfwWindow, double x, double y) -> void
{
    const auto window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
    window->m_events.emplace_back(CursorEvent{ x, y });
}

auto Window::onScroll(GLFWwindow* glfwWindow, double x, double y) -> void
{
    const auto window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
    window->m_events.emplace_back(ScrollEvent{ x, y });
}

auto Window::onContentScale(GLFWwindow* glfwWindow, float x, float /*y*/) -> void
{
    // One scale: the platforms GLFW supports scale both axes alike.
    const auto window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
    window->m_contentScale = x;
    if (window->m_contentScaleHandler)
        window->m_contentScaleHandler(x);
}

auto Window::onClose(GLFWwindow* glfwWindow) -> void
{
    // GLFW has already set the close flag; a refusal clears it again.
    const auto window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
    if (window->m_closeHandler && !window->m_closeHandler())
        glfwSetWindowShouldClose(glfwWindow, GLFW_FALSE);
}
}
