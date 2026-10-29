export module aegis.editor.core:console;
import std;
import aegis.core;

export namespace aegis::editor
{
/// @brief One line of the console: a log message with its level, numbered in the order it arrived.
struct ConsoleEntry
{
    log::Level level{ log::Level::Info };
    std::string message;
    std::uint64_t sequence{ 0 };
    /// @brief How many messages the line stands for: more than 1 when ConsoleFilter::collapse merged repeats into it.
    std::size_t repeats{ 1 };
};

/// @brief How many messages of each level arrived since the last clear (Error includes Fatal), also those the buffer
///        dropped since.
struct ConsoleCounts
{
    std::size_t info{ 0 };
    std::size_t warnings{ 0 };
    std::size_t errors{ 0 };
};

/// @brief What the console shows: the levels that are on (Error includes Fatal) and a search text.
struct ConsoleFilter
{
    bool info{ true };
    bool warn{ true };
    bool error{ true };
    /// @brief Shown only if the message contains it, ignoring case. Empty matches all.
    std::string search;
    /// @brief Merges consecutive shown messages of one level that are the same, or differ only in their numbers, into
    ///        the first of them (ConsoleEntry::repeats).
    bool collapse{ false };

    [[nodiscard]] auto matches(const ConsoleEntry& entry) const -> bool;
};

/// @brief The most recent log messages, up to a capacity (the oldest are dropped). Safe to fill from any
///        thread, as a log sink is called from wherever the message is logged.
class ConsoleBuffer
{
public:
    explicit ConsoleBuffer(std::size_t capacity = 2000);

    /// @brief Adds a message; the oldest entry goes when the buffer is full.
    void push(log::Level level, std::string_view message);
    /// @brief Removes all entries, resets the counts and forgets the last problem.
    void clear();

    /// @brief The entries 'filter' accepts, oldest first (a copy: the buffer may change at any time).
    [[nodiscard]] auto entries(const ConsoleFilter& filter = {}) const -> std::vector<ConsoleEntry>;
    /// @brief The entries 'filter' accepts as lines of `[level] message`, for copying.
    [[nodiscard]] auto text(const ConsoleFilter& filter = {}) const -> std::string;
    [[nodiscard]] auto size() const -> std::size_t;
    [[nodiscard]] auto counts() const -> ConsoleCounts;
    /// @brief The latest warning or error, which stays after the buffer dropped it; none after clear.
    [[nodiscard]] auto lastProblem() const -> std::optional<ConsoleEntry>;

private:
    mutable std::mutex m_mutex;
    std::size_t m_capacity;
    std::deque<ConsoleEntry> m_entries;
    std::optional<ConsoleEntry> m_lastProblem;
    ConsoleCounts m_counts;
    std::uint64_t m_nextSequence{ 0 };
};
}
