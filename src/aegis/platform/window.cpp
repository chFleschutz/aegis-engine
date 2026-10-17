module;
#include "GLFW/glfw3.h"

#include <cassert>

module aegis.platform;

import std;

namespace aegis::platform
{
Window::Window(const Desc& desc) :
    m_width{ desc.width },
    m_height{ desc.height }
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    const std::string title{ desc.title }; // GLFW reads a null-terminated string
    m_window = glfwCreateWindow(static_cast<int>(desc.width),
        static_cast<int>(desc.height),
        title.c_str(),
        nullptr,
        nullptr);
    assert(m_window && "Platform Error: Failed to create window");

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
        glfwTerminate();
    }
}

auto Window::operator=(Window&& other) noexcept -> Window&
{
    // Swapping hands this window's old GLFW window to 'other', which destroys it.
    std::swap(m_window, other.m_window);
    std::swap(m_width, other.m_width);
    std::swap(m_height, other.m_height);
    std::swap(m_wasResized, other.m_wasResized);
    std::swap(m_keyHandler, other.m_keyHandler);
    if (m_window)
        glfwSetWindowUserPointer(m_window, this);
    if (other.m_window)
        glfwSetWindowUserPointer(other.m_window, &other);
    return *this;
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
