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
    m_height{ desc.height }
{
    glfwSetWindowUserPointer(m_window, this);
    // Framebuffer size, not window size: it is what a swapchain must match, and update() reads it too.
    glfwSetFramebufferSizeCallback(m_window, onFramebufferResize);
    glfwSetKeyCallback(m_window, onKey);
}

Window::Window(Window&& other) noexcept :
    m_window{ std::exchange(other.m_window, nullptr) },
    m_width{ other.m_width },
    m_height{ other.m_height },
    m_wasResized{ other.m_wasResized },
    m_keyHandler{ std::move(other.m_keyHandler) }
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
    if (window->m_keyHandler)
        window->m_keyHandler(key, action, mods);
}
}
