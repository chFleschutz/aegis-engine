export module aegis.world:time;

import std;

export namespace aegis::world
{
/// @brief Splits a world's frame time into steps of a fixed length: an accumulator gathers the frames' deltas,
///        and each update runs as many whole steps as it holds, at most maxSteps.
/// @note A frame worth more steps than the cap drops the whole steps beyond it and keeps the fraction, so a
///       slow frame doesn't snowball into slower ones; the simulation then runs slower than the wall clock.
class FixedStep
{
public:
    static constexpr float DEFAULT_STEP_SEC{ 1.0f / 60.0f };
    static constexpr std::uint32_t DEFAULT_MAX_STEPS{ 8 };

    FixedStep() = default;
    /// @param stepSeconds The length of a step, above zero
    /// @param maxSteps The most steps one frame runs, at least one
    explicit FixedStep(float stepSeconds, std::uint32_t maxSteps = DEFAULT_MAX_STEPS);

    /// @brief Adds a frame's delta and returns the number of steps it completes (at most maxSteps)
    [[nodiscard]] auto advance(float deltaSeconds) -> std::uint32_t;

    [[nodiscard]] auto step() const -> float { return m_step; }
    [[nodiscard]] auto maxSteps() const -> std::uint32_t { return m_maxSteps; }
    /// @brief The time gathered towards the next step, in seconds: less than one step after advance
    [[nodiscard]] auto remainder() const -> double { return m_accumulator; }
    /// @brief The fraction of a step left over after the last advance, in [0, 1): how far the present lies
    ///        between the last step and the next, for interpolation
    [[nodiscard]] auto alpha() const -> float { return static_cast<float>(m_accumulator / m_step); }

    /// @brief Changes the length of the steps; the time gathered so far carries over
    void setStep(float stepSeconds);
    void setMaxSteps(std::uint32_t maxSteps);

private:
    float m_step{ DEFAULT_STEP_SEC };
    std::uint32_t m_maxSteps{ DEFAULT_MAX_STEPS };
    double m_accumulator{ 0.0 };
};

/// @brief A world's time as its systems, scripts and the renderer read it (World::time, Registry::time)
/// @note World::update writes it before any system runs. The frame delta, elapsed time and frame count advance in
///       every update; the steps run only in UpdateMode::Play, so alpha and steps stand still in Edit.
struct Time
{
    float deltaSeconds{ 0.0f };                            ///< This update's frame delta
    float fixedStepSeconds{ FixedStep::DEFAULT_STEP_SEC }; ///< The length of a fixed step
    float alpha{ 0.0f };                                   ///< The fraction of a step left over after this update's steps
    double elapsedSeconds{ 0.0 };                          ///< The sum of the frame deltas so far
    std::uint64_t frame{ 0 };                              ///< The updates so far, this one included
    std::uint64_t steps{ 0 };                              ///< The fixed steps so far; within one, it counts that one too
};
}
