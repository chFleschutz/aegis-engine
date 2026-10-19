export module aegis.base:log;

import std;

// Logging that works with no setup: lines go to stderr as `[warn] message`. Safe to call from any
// thread, before or without an Engine. Library code logs only degraded behaviour that carries on;
// failures that stop an operation are returned as errors instead.
export namespace aegis::log
{
enum class Level
{
    Info,
    Warn,
    Error,
    Fatal,
};

/// @brief Receives every message instead of stderr. 'message' has no severity tag or newline.
/// @note Called with the log mutex held, so a sink must not log itself.
using Sink = std::function<void(Level, std::string_view)>;

/// @brief Lower-case tag of a level as written to stderr ("info", "warn", ...).
[[nodiscard]] auto levelName(Level level) noexcept -> std::string_view;

/// @brief Replaces where messages go (tests capture output with it, an app can forward to a file).
///        An empty sink restores stderr.
void setSink(Sink sink);

namespace detail
{
void write(Level level, std::string_view message);
}

template<typename... Args>
void info(std::format_string<Args...> format, Args&&... args)
{
    detail::write(Level::Info, std::format(format, std::forward<Args>(args)...));
}

template<typename... Args>
void warn(std::format_string<Args...> format, Args&&... args)
{
    detail::write(Level::Warn, std::format(format, std::forward<Args>(args)...));
}

template<typename... Args>
void error(std::format_string<Args...> format, Args&&... args)
{
    detail::write(Level::Error, std::format(format, std::forward<Args>(args)...));
}

/// @brief Logs an unrecoverable failure. Does not terminate; the caller decides how to stop.
template<typename... Args>
void fatal(std::format_string<Args...> format, Args&&... args)
{
    detail::write(Level::Fatal, std::format(format, std::forward<Args>(args)...));
}
}
