import std;
import aegis.game;

namespace math = aegis::math;

namespace
{
/// @brief Walks its entity's CharacterController by the input axes MoveX (along the world's right axis) and MoveY
///        (along its forward axis), and jumps on the input action Jump while grounded
class CharacterInput : public aegis::scripting::Script
{
public:
    float speed{ 4.0f };     // Meters per second, a registered field
    float jumpSpeed{ 5.0f }; // The take-off speed in meters per second, a registered field

    void onCreate(aegis::scripting::ScriptContext& context) override
    {
        if (!context.has<aegis::physics::CharacterController>())
            aegis::log::warn("CharacterInput on '{}' has no CharacterController to drive", context.get<aegis::Name>().name);
    }

    void onFixedUpdate(aegis::scripting::ScriptContext& context, float) override
    {
        if (!context.has<aegis::physics::CharacterController>())
            return;
        const auto& input = context.input();
        auto walk = math::world::right<float> * input.axis("MoveX") + math::world::forward<float> * input.axis("MoveY");
        if (math::length(walk) > 1.0f)
            walk = math::normalize(walk); // A diagonal is no faster

        auto& controller = context.get<aegis::physics::CharacterController>();
        controller.desiredVelocity = walk * speed;
        if (m_jump && controller.grounded)
            controller.desiredVelocity += math::world::up<float> * jumpSpeed;
        m_jump = false;
    }

    void onUpdate(aegis::scripting::ScriptContext& context, float) override
    {
        // A press lasts one frame, which may run no fixed step: keep it for the next one
        if (context.input().action("Jump").pressed())
            m_jump = true;
    }

private:
    bool m_jump{ false };
};
}

namespace examples
{
auto registerCharacterInput(aegis::reflect::TypeRegistry& types) -> std::expected<void, aegis::reflect::Error>
{
    return aegis::scripting::registerScript<CharacterInput>(types, "CharacterInput").and_then([](auto builder) {
        return builder.field("speed", &CharacterInput::speed).field("jumpSpeed", &CharacterInput::jumpSpeed).status();
    });
}
}
