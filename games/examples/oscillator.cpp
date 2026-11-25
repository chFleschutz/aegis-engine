import std;
import aegis.game;

namespace math = aegis::math;

namespace
{
/// @brief Moves its entity back and forth along 'offset' from where it starts, once every 'period' seconds, in the
///        world's fixed steps: a kinematic body (a moving platform) follows it the same at any frame rate
/// @note Give the entity and its children a DynamicTag: they move.
class Oscillator : public aegis::scripting::Script
{
public:
    math::Float3 offset{ 0.0f, 0.0f, 1.0f }; // The farthest it moves from its start, a registered field
    float period{ 4.0f };                    // Seconds, a registered field

    void onCreate(aegis::scripting::ScriptContext& context) override { m_start = context.get<aegis::Transform>().location; }

    void onFixedUpdate(aegis::scripting::ScriptContext& context, float) override
    {
        if (period <= 0.0f)
            return;
        // From the step count, not a sum of steps, so it doesn't drift
        const auto& time = context.time();
        const auto seconds = static_cast<double>(time.steps) * time.fixedStepSeconds;
        const auto phase = static_cast<float>(std::fmod(seconds / period, 1.0)) * 2.0f * std::numbers::pi_v<float>;
        context.get<aegis::Transform>().location = m_start + offset * std::sin(phase);
    }

private:
    math::Float3 m_start{ 0.0f };
};
}

namespace examples
{
auto registerOscillator(aegis::reflect::TypeRegistry& types) -> std::expected<void, aegis::reflect::Error>
{
    return aegis::scripting::registerScript<Oscillator>(types, "Oscillator").and_then([](auto builder) {
        return builder.field("offset", &Oscillator::offset).field("period", &Oscillator::period).status();
    });
}
}
