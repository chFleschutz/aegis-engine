module;
#include <GLFW/glfw3.h>

#include <string_view>

export module aegis.platform.window;

export namespace aegis::platform
{
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
    ~Window();

    [[nodiscard]] auto glfwWindow() const -> GLFWwindow* { return m_window; }
    [[nodiscard]] auto width() const -> uint32_t { return m_width; }
    [[nodiscard]] auto height() const -> uint32_t { return m_height; }
    [[nodiscard]] auto wasResized() const -> bool { return m_wasResized; }
    [[nodiscard]] auto shouldClose() const -> bool;

    auto pollEvents() const -> void;

private:
    static void onWindowResize(GLFWwindow* glfwWindow, int newWidth, int newHeight);

    GLFWwindow* m_window{ nullptr };
    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };
    bool m_wasResized{ false };
};
}
