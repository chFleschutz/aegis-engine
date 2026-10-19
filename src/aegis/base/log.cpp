module;

#include <cstdio>

module aegis.base;

import std;

namespace aegis::log
{
namespace
{
std::mutex g_mutex;
Sink g_sink;
}

auto levelName(Level level) noexcept -> std::string_view
{
    switch (level)
    {
    case Level::Info:
        return "info";
    case Level::Warn:
        return "warn";
    case Level::Error:
        return "error";
    case Level::Fatal:
        return "fatal";
    }
    std::unreachable();
}

void setSink(Sink sink)
{
    std::lock_guard lock{ g_mutex };
    g_sink = std::move(sink);
}

namespace detail
{
void write(Level level, std::string_view message)
{
    std::lock_guard lock{ g_mutex };
    if (g_sink)
    {
        g_sink(level, message);
        return;
    }
    std::println(stderr, "[{}] {}", levelName(level), message);
}
}
}
