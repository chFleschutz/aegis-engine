export module aegis.core.profiler;

import std;

export namespace aegis
{
class Timer
{
public:
    Timer() :
        m_start{ std::chrono::high_resolution_clock::now() }
    {
    }
    ~Timer() = default;

    void reStart() { m_start = std::chrono::high_resolution_clock::now(); }

    template<typename Unit>
    auto elapsed() const -> Unit
    {
        const auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<Unit>(end - m_start);
    }

    template<typename T, typename Unit>
    auto elapsed() const -> T
    {
        const auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<T, Unit>(end - m_start).count();
    }

    auto elapsedNanos() const -> double { return elapsed<double, std::nano>(); }
    auto elapsedMicros() const -> double { return elapsed<double, std::micro>(); }
    auto elapsedMillis() const -> double { return elapsed<double, std::milli>(); }
    auto elapsedSeconds() const -> double { return elapsed<double, std::ratio<1>>(); }

private:
    std::chrono::time_point<std::chrono::high_resolution_clock> m_start;
};

/// @brief Rolling average over N values
/// @tparam N Number of values to average (must be greater than zero)
template<std::size_t N>
    requires(N > 0)
class RollingAverage
{
public:
    RollingAverage() = default;
    RollingAverage(const RollingAverage&) = default;
    RollingAverage(RollingAverage&&) = default;
    ~RollingAverage() = default;

    RollingAverage& operator=(const RollingAverage&) = default;
    RollingAverage& operator=(RollingAverage&&) = default;

    operator double() const { return average(); }
    void operator+=(double value) { add(value); }

    /// @brief Add a new value to the rolling average
    void add(double value)
    {
        m_sum -= m_values[m_index];
        m_values[m_index] = value;
        m_sum += value;
        m_index = (m_index + 1) % N;
    }

    /// @brief Get the average of the N values
    /// @note Average is dirty for the first N-1 values
    auto average() const -> double
    {
        return m_sum / N;
    }

    auto last() const -> double
    {
        std::size_t lastIndex = (m_index + N - 1) % N;
        return m_values[lastIndex];
    }

private:
    std::array<double, N> m_values{};
    double m_sum = 0.0;
    std::size_t m_index = 0;
};

/// @brief Utility class for profiling code execution
/// @note Uses a rolling average over 'AVERAGE_FRAME_COUNT' frames
class Profiler
{
public:
    static constexpr int AVERAGE_FRAME_COUNT = 50;

    using TimeMap = std::unordered_map<std::string, RollingAverage<AVERAGE_FRAME_COUNT>>;

    Profiler(const Profiler&) = delete;
    Profiler(Profiler&&) = delete;
    ~Profiler() = default;

    auto operator=(const Profiler&) -> Profiler& = delete;
    auto operator=(Profiler&&) -> Profiler& = delete;

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
    Profiler() = default;

    TimeMap m_times;
};

/// @brief Times the current scope and adds the time to the profiler
class ScopeProfiler
{
public:
    ScopeProfiler(std::string_view name) :
        m_name(name)
    {
    }

    ~ScopeProfiler()
    {
        Profiler::instance().addTime(m_name, m_timer.elapsedMillis());
    }

private:
    Timer m_timer;
    std::string m_name;
};
}
