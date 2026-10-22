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
};

/// @brief What the console shows: the levels that are on (Error includes Fatal) and a search text.
struct ConsoleFilter
{
    bool info{ true };
    bool warn{ true };
    bool error{ true };
    /// @brief Shown only if the message contains it, ignoring case. Empty matches all.
    std::string search;

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
    /// @brief Removes all entries and forgets the last problem.
    void clear();

    /// @brief The entries 'filter' accepts, oldest first (a copy: the buffer may change at any time).
    [[nodiscard]] auto entries(const ConsoleFilter& filter = {}) const -> std::vector<ConsoleEntry>;
    /// @brief The entries 'filter' accepts as lines of `[level] message`, for copying.
    [[nodiscard]] auto text(const ConsoleFilter& filter = {}) const -> std::string;
    [[nodiscard]] auto size() const -> std::size_t;
    /// @brief The latest warning or error, which stays after the buffer dropped it; none after clear.
    [[nodiscard]] auto lastProblem() const -> std::optional<ConsoleEntry>;

private:
    mutable std::mutex m_mutex;
    std::size_t m_capacity;
    std::deque<ConsoleEntry> m_entries;
    std::optional<ConsoleEntry> m_lastProblem;
    std::uint64_t m_nextSequence{ 0 };
};
}
