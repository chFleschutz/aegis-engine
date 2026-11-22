export module aegis.platform:glfw_state;

import std;
import :error;

// The GLFW-free parts of window setup: what the GLFW calls in window.cpp report and count. Kept apart
// so they can be tested without a display.
export namespace aegis::platform::detail
{
/// @brief Remembers the last error GLFW reported, which GLFW hands to an error callback only.
class GlfwErrorCapture
{
public:
    auto record(int code, std::string_view description) -> void
    {
        const std::scoped_lock lock{ m_mutex };
        m_message = std::format("{} (GLFW error {:#x})", description, static_cast<unsigned>(code));
    }

    /// @brief The last recorded text, or empty if GLFW reported nothing since the last call.
    [[nodiscard]] auto take() -> std::string
    {
        const std::scoped_lock lock{ m_mutex };
        return std::exchange(m_message, {});
    }

private:
    std::mutex m_mutex;
    std::string m_message;
};

/// @brief Counts the users of a process-wide resource: the first acquire initializes it, the last
/// release tears it down.
class InitRefCount
{
public:
    /// @brief Runs init() for the first user; false (and no user added) when init() fails.
    template<typename Init>
    [[nodiscard]] auto acquire(Init&& init) -> bool
    {
        const std::scoped_lock lock{ m_mutex };
        if (m_count == 0 && !init())
            return false;
        ++m_count;
        return true;
    }

    /// @brief Runs terminate() when the last user leaves. Releasing with no user is ignored.
    template<typename Terminate>
    auto release(Terminate&& terminate) -> void
    {
        const std::scoped_lock lock{ m_mutex };
        if (m_count == 0)
            return;
        if (--m_count == 0)
            terminate();
    }

    [[nodiscard]] auto count() -> std::size_t
    {
        const std::scoped_lock lock{ m_mutex };
        return m_count;
    }

private:
    std::mutex m_mutex;
    std::size_t m_count{ 0 };
};

/// @brief The error for a failed glfwInit; 'reason' is GLFW's last error text, possibly empty.
[[nodiscard]] inline auto initError(std::string_view reason) -> Error
{
    auto message = std::string{ "Failed to initialize GLFW" };
    message += reason.empty() ? std::string{ " (no reason given)" } : std::format(": {}", reason);
    return Error{ .code = ErrorCode::InitFailed, .message = std::move(message) };
}

/// @brief The error for a failed glfwCreateWindow.
[[nodiscard]] inline auto windowError(std::string_view reason) -> Error
{
    auto message = std::string{ "Failed to create window" };
    message += reason.empty() ? std::string{ " (no reason given)" } : std::format(": {}", reason);
    return Error{ .code = ErrorCode::WindowCreationFailed, .message = std::move(message) };
}
}
