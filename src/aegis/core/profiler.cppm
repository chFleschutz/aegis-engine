export module aegis.core:profiler;

import std;

// Stays in namespace aegis (not aegis::core): the engine and the scene use these names unqualified.
export namespace aegis
{
/// @brief Stopwatch on the monotonic clock.
class Timer
{
public:
    using Clock = std::chrono::steady_clock;

    Timer() :
        m_start{ Clock::now() }
    {
    }

    void reStart() { m_start = Clock::now(); }

    template<typename Unit>
    auto elapsed() const -> Unit
    {
        return std::chrono::duration_cast<Unit>(Clock::now() - m_start);
    }

    template<typename T, typename Unit>
    auto elapsed() const -> T
    {
        return std::chrono::duration<T, Unit>(Clock::now() - m_start).count();
    }

    auto elapsedNanos() const -> double { return elapsed<double, std::nano>(); }
    auto elapsedMicros() const -> double { return elapsed<double, std::micro>(); }
    auto elapsedMillis() const -> double { return elapsed<double, std::milli>(); }
    auto elapsedSeconds() const -> double { return elapsed<double, std::ratio<1>>(); }

private:
    Clock::time_point m_start;
};

/// @brief Rolling average over the last N values
/// @tparam N Number of values to average (must be greater than zero)
template<std::size_t N>
    requires(N > 0)
class RollingAverage
{
public:
    operator double() const { return average(); }
    void operator+=(double value) { add(value); }

    /// @brief Add a new value to the rolling average
    void add(double value)
    {
        m_sum -= m_values[m_index];
        m_values[m_index] = value;
        m_sum += value;
        m_index = (m_index + 1) % N;
        m_count = std::min(m_count + 1, N);
    }

    /// @brief Average of the values added so far, at most the last N; 0 when there are none
    auto average() const -> double
    {
        return m_count == 0 ? 0.0 : m_sum / static_cast<double>(m_count);
    }

    /// @brief The most recently added value (0 when there is none)
    auto last() const -> double
    {
        std::size_t lastIndex = (m_index + N - 1) % N;
        return m_values[lastIndex];
    }

    /// @brief Number of values the average covers (up to N)
    auto count() const -> std::size_t { return m_count; }

private:
    std::array<double, N> m_values{};
    double m_sum = 0.0;
    std::size_t m_index = 0;
    std::size_t m_count = 0;
};

/// @brief Utility class for profiling code execution
/// @note Uses a rolling average over 'AVERAGE_FRAME_COUNT' frames. The engine records into the
///       one default instance, instance(); tests and tools can create their own.
class Profiler
{
public:
    static constexpr std::size_t AVERAGE_FRAME_COUNT = 50;

    using TimeMap = std::unordered_map<std::string, RollingAverage<AVERAGE_FRAME_COUNT>>;

    /// @brief The default instance, shared by everything that does not pass its own.
    [[nodiscard]] static auto instance() -> Profiler&
    {
        static Profiler instance;
        return instance;
    }

    /// @brief Retrieve the average time for a given name or 0.0 if not found
    [[nodiscard]] auto time(const std::string& name) const -> double
    {
        auto it = m_times.find(name);
        if (it == m_times.end())
            return 0.0;
        return it->second.average();
    }

    /// @brief Retrieve the last recorded time
    [[nodiscard]] auto lastTime(const std::string& name) const -> double
    {
        auto it = m_times.find(name);
        if (it == m_times.end())
            return 0.0;
        return it->second.last();
    }

    [[nodiscard]] auto times() const -> const TimeMap&
    {
        return m_times;
    }

    void addTime(const std::string& name, double time)
    {
        m_times[name].add(time);
    }

private:
    TimeMap m_times;
};

/// @brief Times the current scope and adds the time (in ms) to a profiler
class ScopeProfiler
{
public:
    explicit ScopeProfiler(std::string_view name, Profiler& profiler = Profiler::instance()) :
        m_name(name),
        m_profiler{ profiler }
    {
    }

    ScopeProfiler(const ScopeProfiler&) = delete;
    auto operator=(const ScopeProfiler&) -> ScopeProfiler& = delete;

    ~ScopeProfiler()
    {
        m_profiler.addTime(m_name, m_timer.elapsedMillis());
    }

private:
    Timer m_timer;
    std::string m_name;
    Profiler& m_profiler;
};
}
