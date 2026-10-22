module;
#include <GLFW/glfw3.h>

export module aegis.platform:window;

import std;
import :error;
import :input_state;
import :key;

export namespace aegis::platform
{
class Input;

class Window
{
public:
    struct Desc
    {
        std::string_view title;
        std::uint32_t width;
        std::uint32_t height;
    };

    /// @brief Opens a window. Fails with GLFW's reason when there is no display or GLFW refuses the window.
    [[nodiscard]] static auto create(const Desc& desc) -> std::expected<Window, Error>;

    Window(const Window&) = delete;
    Window(Window&& other) noexcept;
    ~Window();

    auto operator=(const Window&) -> Window& = delete;
    auto operator=(Window&&) -> Window& = delete;

    [[nodiscard]] auto glfwWindow() const -> GLFWwindow* { return m_window; }
    [[nodiscard]] auto width() const -> std::uint32_t { return m_width; }
    [[nodiscard]] auto height() const -> std::uint32_t { return m_height; }
    [[nodiscard]] auto extent() const -> std::pair<std::uint32_t, std::uint32_t> { return { m_width, m_height }; }
    [[nodiscard]] auto isMinimized() const -> bool { return m_width == 0 || m_height == 0; }
    [[nodiscard]] auto wasResized() const -> bool { return m_wasResized; }
    [[nodiscard]] auto shouldClose() const -> bool;

    /// @brief The ratio of the monitor's DPI to the platform default: 1.5 at 150% display scaling.
    [[nodiscard]] auto contentScale() const -> float { return m_contentScale; }

    /// @brief Calls 'handler' with the new content scale when it changes, e.g. when the window moves to a
    /// monitor with another scale. Runs inside update() and waitEvents().
    auto setContentScaleHandler(std::function<void(float scale)> handler) -> void { m_contentScaleHandler = std::move(handler); }

    [[nodiscard]] auto title() const -> std::string_view { return m_title; }
    auto setTitle(std::string_view title) -> void;

    /// @brief Asks 'canClose' when the user closes the window; false keeps it open, e.g. to ask about
    /// unsaved changes first. Without a handler the window closes. Runs inside update() and waitEvents().
    auto setCloseHandler(std::function<bool()> canClose) -> void { m_closeHandler = std::move(canClose); }

    /// @brief Makes shouldClose() true without asking the close handler, e.g. once the user confirmed.
    auto close() -> void;

    /// @brief Processes pending events: runs key bindings, collects the input events and updates the size.
    auto update() -> void;

    /// @brief Blocks until an event arrives, then processes it (e.g. while minimized).
    auto waitEvents() -> void;

    /// @brief The input events since the last call, in order. Input::update takes them each frame.
    [[nodiscard]] auto takeEvents() -> std::vector<InputEvent> { return std::exchange(m_events, {}); }

    auto resetResized() -> void { m_wasResized = false; }

private:
    Window(GLFWwindow* window, const Desc& desc);

    // Input receives this window's key events through m_keyHandler. GLFW allows one callback per
    // event type and window, so the window owns them all and routes events onward.
    friend class Input;

    static auto onFramebufferResize(GLFWwindow* glfwWindow, int newWidth, int newHeight) -> void;
    static auto onKey(GLFWwindow* glfwWindow, int key, int scancode, int action, int mods) -> void;
    static auto onMouseButton(GLFWwindow* glfwWindow, int button, int action, int mods) -> void;
    static auto onCursorPos(GLFWwindow* glfwWindow, double x, double y) -> void;
    static auto onScroll(GLFWwindow* glfwWindow, double x, double y) -> void;
    static auto onContentScale(GLFWwindow* glfwWindow, float x, float y) -> void;
    static auto onClose(GLFWwindow* glfwWindow) -> void;

    GLFWwindow* m_window{ nullptr };
    std::uint32_t m_width{ 0 };
    std::uint32_t m_height{ 0 };
    bool m_wasResized{ false };
    float m_contentScale{ 1.0f };
    std::string m_title;
    std::function<void(int key, int action, int mods)> m_keyHandler;
    std::function<void(float scale)> m_contentScaleHandler;
    std::function<bool()> m_closeHandler;
    std::vector<InputEvent> m_events;
};
}
