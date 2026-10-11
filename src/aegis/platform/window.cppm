module;
#include <GLFW/glfw3.h>

#include <cstdint>
#include <functional>
#include <string_view>
#include <utility>

export module aegis.platform:window;

export namespace aegis::platform
{
class Input;

class Window
{
public:
    struct Desc
    {
        std::string_view title;
        uint32_t width;
        uint32_t height;
    };

    explicit Window(const Desc& desc);
    Window(const Window&) = delete;
    Window(Window&& other) noexcept;
    ~Window();

    auto operator=(const Window&) -> Window& = delete;
    auto operator=(Window&& other) noexcept -> Window&;

    [[nodiscard]] auto glfwWindow() const -> GLFWwindow* { return m_window; }
    [[nodiscard]] auto width() const -> uint32_t { return m_width; }
    [[nodiscard]] auto height() const -> uint32_t { return m_height; }
    [[nodiscard]] auto extent() const -> std::pair<uint32_t, uint32_t> { return { m_width, m_height }; }
    [[nodiscard]] auto isMinimized() const -> bool { return m_width == 0 || m_height == 0; }
    [[nodiscard]] auto wasResized() const -> bool { return m_wasResized; }
    [[nodiscard]] auto shouldClose() const -> bool;

    /// @brief Processes pending events: runs input callbacks and updates the size.
    auto update() -> void;

    /// @brief Blocks until an event arrives, then processes it (e.g. while minimized).
    auto waitEvents() -> void;

    auto resetResized() -> void { m_wasResized = false; }

private:
    // Input receives this window's key events through m_keyHandler. GLFW allows one callback per
    // event type and window, so the window owns them all and routes events onward.
    friend class Input;

    static auto onFramebufferResize(GLFWwindow* glfwWindow, int newWidth, int newHeight) -> void;
    static auto onKey(GLFWwindow* glfwWindow, int key, int scancode, int action, int mods) -> void;

    GLFWwindow* m_window{ nullptr };
    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };
    bool m_wasResized{ false };
    std::function<void(int key, int action, int mods)> m_keyHandler;
};
}
