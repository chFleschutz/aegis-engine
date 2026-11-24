module;

#include <cassert>

module aegis.world;

import std;

namespace aegis::world
{
FixedStep::FixedStep(float stepSeconds, std::uint32_t maxSteps)
{
    setStep(stepSeconds);
    setMaxSteps(maxSteps);
}

auto FixedStep::advance(float deltaSeconds) -> std::uint32_t
{
    assert(deltaSeconds >= 0.0f && "FixedStep::advance: a negative delta");
    m_accumulator += deltaSeconds;

    const double step = m_step;
    std::uint32_t steps = 0;
    while (m_accumulator >= step && steps < m_maxSteps)
    {
        m_accumulator -= step;
        ++steps;
    }
    if (m_accumulator >= step) // Past the cap: drop the whole steps, keep the fraction
        m_accumulator = std::fmod(m_accumulator, step);
    return steps;
}

void FixedStep::setStep(float stepSeconds)
{
    assert(stepSeconds > 0.0f && "FixedStep::setStep: a step must be longer than zero");
    m_step = stepSeconds;
}

void FixedStep::setMaxSteps(std::uint32_t maxSteps)
{
    assert(maxSteps > 0 && "FixedStep::setMaxSteps: a frame must be able to run a step");
    m_maxSteps = maxSteps;
}
}
