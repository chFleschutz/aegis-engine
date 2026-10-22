module;

#include <cstdio>

module aegis.core;

import std;

namespace aegis::log
{
namespace
{
std::mutex g_mutex;
Sink g_sink;
std::vector<std::pair<SinkId, Sink>> g_sinks;
std::uint32_t g_nextSinkId{ 1 };
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

auto addSink(Sink sink) -> SinkId
{
    std::lock_guard lock{ g_mutex };
    const SinkId id{ g_nextSinkId++ };
    g_sinks.emplace_back(id, std::move(sink));
    return id;
}

void removeSink(SinkId id)
{
    std::lock_guard lock{ g_mutex };
    std::erase_if(g_sinks, [id](const auto& entry) { return entry.first == id; });
}

namespace detail
{
void write(Level level, std::string_view message)
{
    std::lock_guard lock{ g_mutex };
    if (g_sink)
        g_sink(level, message);
    else
        std::println(stderr, "[{}] {}", levelName(level), message);

    for (const auto& [id, sink] : g_sinks)
        sink(level, message);
}
}
}
