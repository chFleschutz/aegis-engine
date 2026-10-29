module aegis.editor.core;
import std;
import aegis.core;

namespace aegis::editor
{
namespace
{
auto isDigit(char c) -> bool
{
    return c >= '0' && c <= '9';
}

// Whether 'a' and 'b' are the same once each run of digits counts as the same: "Material 3 has no name" and
// "Material 12 has no name" are.
auto sameShape(std::string_view a, std::string_view b) -> bool
{
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < a.size() && j < b.size())
    {
        if (isDigit(a[i]) && isDigit(b[j]))
        {
            while (i < a.size() && isDigit(a[i]))
                ++i;
            while (j < b.size() && isDigit(b[j]))
                ++j;
        }
        else if (a[i++] != b[j++])
            return false;
    }
    return i == a.size() && j == b.size();
}
}

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
    if (level == log::Level::Info)
        ++m_counts.info;
    else
    {
        ++(level == log::Level::Warn ? m_counts.warnings : m_counts.errors);
        m_lastProblem = entry;
    }
    if (m_entries.size() == m_capacity)
        m_entries.pop_front();
    m_entries.push_back(std::move(entry));
}

void ConsoleBuffer::clear()
{
    std::lock_guard lock{ m_mutex };
    m_entries.clear();
    m_lastProblem.reset();
    m_counts = {};
}

auto ConsoleBuffer::entries(const ConsoleFilter& filter) const -> std::vector<ConsoleEntry>
{
    std::lock_guard lock{ m_mutex };
    std::vector<ConsoleEntry> result;
    for (const auto& entry : m_entries)
    {
        if (!filter.matches(entry))
            continue;
        if (filter.collapse && !result.empty() && result.back().level == entry.level && sameShape(result.back().message, entry.message))
            ++result.back().repeats;
        else
            result.push_back(entry);
    }
    return result;
}

auto ConsoleBuffer::text(const ConsoleFilter& filter) const -> std::string
{
    std::string result;
    for (const auto& entry : entries(filter))
    {
        result += std::format("[{}] {}", log::levelName(entry.level), entry.message);
        result += entry.repeats > 1 ? std::format(" (×{})\n", entry.repeats) : "\n";
    }
    return result;
}

auto ConsoleBuffer::size() const -> std::size_t
{
    std::lock_guard lock{ m_mutex };
    return m_entries.size();
}

auto ConsoleBuffer::counts() const -> ConsoleCounts
{
    std::lock_guard lock{ m_mutex };
    return m_counts;
}

auto ConsoleBuffer::lastProblem() const -> std::optional<ConsoleEntry>
{
    std::lock_guard lock{ m_mutex };
    return m_lastProblem;
}
}
