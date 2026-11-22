export module aegis.core:frame_clock;

import std;

export namespace aegis::core
{
/// @brief Hands out the delta time of each frame
///
/// Two kinds. A clamped clock measures the wall clock and caps the result, so a stall (a breakpoint,
/// a load, a minimized window) doesn't reach scripts and systems as one huge step. A fixed clock
/// returns the same step every frame whatever the time, for tests and `--frames` runs.
class FrameClock
{
public:
    using Clock = std::chrono::steady_clock;

    /// @brief Wall clock time, at most maxDeltaSec per frame
    [[nodiscard]] static auto clamped(float maxDeltaSec = 0.1f) -> FrameClock { return FrameClock{ maxDeltaSec, false }; }
    /// @brief The same step every frame
    [[nodiscard]] static auto fixed(float stepSec) -> FrameClock { return FrameClock{ stepSec, true }; }

    /// @brief Starts a new frame at the given time and returns its delta in seconds
    /// @note The first frame after construction has a delta of zero unless reset was called (fixed clocks: the step).
    auto tick(Clock::time_point now) -> float
    {
        if (m_fixed)
            m_delta = m_limit;
        else if (m_last)
            m_delta = std::min(std::chrono::duration<float>(now - *m_last).count(), m_limit);
        else
            m_delta = 0.0f;
        m_last = now;
        m_elapsed += m_delta;
        ++m_frame;
        return m_delta;
    }

    /// @brief Forgets the time of the last frame: the next delta doesn't include what passed until now
    /// @note Call it after the app was paused (minimized window) or before the first frame.
    void reset(Clock::time_point now) { m_last = now; }

    /// @brief The delta of the current frame, in seconds
    [[nodiscard]] auto delta() const -> float { return m_delta; }
    /// @brief The sum of all deltas handed out, in seconds
    [[nodiscard]] auto elapsed() const -> double { return m_elapsed; }
    /// @brief The number of frames started
    [[nodiscard]] auto frame() const -> std::uint64_t { return m_frame; }
    [[nodiscard]] auto isFixed() const -> bool { return m_fixed; }

private:
    FrameClock(float limit, bool fixed) :
        m_limit(limit),
        m_fixed(fixed)
    {
    }

    float m_limit;
    bool m_fixed;
    std::optional<Clock::time_point> m_last;
    float m_delta{ 0.0f };
    double m_elapsed{ 0.0 };
    std::uint64_t m_frame{ 0 };
};
}
