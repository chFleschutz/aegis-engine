module;
#include "GLFW/glfw3.h"

#include <cassert>
#include <string_view>

module aegis.platform.window;

namespace aegis::platform
{
Window::Window(const Desc& desc) :
    m_width{ desc.width },
    m_height{ desc.height }
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    m_window = glfwCreateWindow(static_cast<int>(desc.width),
        static_cast<int>(desc.height),
        desc.title.data(),
        nullptr,
        nullptr);
    assert(m_window && "Platform Error: Failed to create window");

    glfwSetWindowUserPointer(m_window, this);
    glfwSetWindowSizeCallback(m_window, onWindowResize);
}

Window::Window(Window&& other) noexcept
{
    std::swap(m_window, other.m_window);
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
    std::swap(m_window, other.m_window);
    if (m_window)
        glfwSetWindowUserPointer(m_window, this);
    return *this;
}

auto Window::shouldClose() const -> bool
{
    return glfwWindowShouldClose(m_window);
}

auto Window::pollEvents() const -> void
{
    glfwPollEvents();
}

auto Window::queryExtent() -> std::pair<uint32_t, uint32_t>
{
    int width, height;
    glfwGetFramebufferSize(m_window, &width, &height);
    m_width = static_cast<uint32_t>(width);
    m_height = static_cast<uint32_t>(height);
    return { m_width, m_height };
}

auto Window::onWindowResize(GLFWwindow* glfwWindow, int newWidth, int newHeight) -> void
{
    const auto window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
    window->m_width = static_cast<uint32_t>(newWidth);
    window->m_height = static_cast<uint32_t>(newHeight);
    window->m_wasResized = true;
}
}
