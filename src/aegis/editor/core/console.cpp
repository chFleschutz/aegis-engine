module aegis.editor.core;
import std;
import aegis.core;

namespace aegis::editor
{
auto ConsoleFilter::matches(const ConsoleEntry& entry) const -> bool
{
    bool levelOn = error; // Error and Fatal
    if (entry.level == log::Level::Info)
        levelOn = info;
    else if (entry.level == log::Level::Warn)
        levelOn = warn;
    return levelOn && core::containsIgnoringCase(entry.message, search);
}

ConsoleBuffer::ConsoleBuffer(std::size_t capacity) :
    m_capacity{ std::max<std::size_t>(capacity, 1) }
{
}

void ConsoleBuffer::push(log::Level level, std::string_view message)
{
    std::lock_guard lock{ m_mutex };
    ConsoleEntry entry{ .level = level, .message = std::string{ message }, .sequence = m_nextSequence++ };
    if (level != log::Level::Info)
        m_lastProblem = entry;
    if (m_entries.size() == m_capacity)
        m_entries.pop_front();
    m_entries.push_back(std::move(entry));
}

void ConsoleBuffer::clear()
{
    std::lock_guard lock{ m_mutex };
    m_entries.clear();
    m_lastProblem.reset();
}

auto ConsoleBuffer::entries(const ConsoleFilter& filter) const -> std::vector<ConsoleEntry>
{
    std::lock_guard lock{ m_mutex };
    std::vector<ConsoleEntry> result;
    for (const auto& entry : m_entries)
    {
        if (filter.matches(entry))
            result.push_back(entry);
    }
    return result;
}

auto ConsoleBuffer::text(const ConsoleFilter& filter) const -> std::string
{
    std::string result;
    for (const auto& entry : entries(filter))
        result += std::format("[{}] {}\n", log::levelName(entry.level), entry.message);
    return result;
}

auto ConsoleBuffer::size() const -> std::size_t
{
    std::lock_guard lock{ m_mutex };
    return m_entries.size();
}

auto ConsoleBuffer::lastProblem() const -> std::optional<ConsoleEntry>
{
    std::lock_guard lock{ m_mutex };
    return m_lastProblem;
}
}
