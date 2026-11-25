import std;
import aegis.game;

namespace
{
/// @brief Starts its entity's AudioSource once, the first time its entity's body starts touching a dynamic body
///        (physics::ContactBegin)
/// @note Give the AudioSource playOnBegin false. A sound of its own would be a clip field, but a scene doesn't load
///       the assets of a script's fields, only those of components.
class ContactSound : public aegis::scripting::Script
{
public:
    void onCreate(aegis::scripting::ScriptContext& context) override
    {
        if (!context.has<aegis::audio::AudioSource>())
            aegis::log::warn("ContactSound on '{}' has no AudioSource to play", context.get<aegis::Name>().name);
    }

    void onUpdate(aegis::scripting::ScriptContext& context, float) override
    {
        if (m_played)
            return;
        auto& registry = context.registry();
        for (const auto& contact : context.events<aegis::physics::ContactBegin>())
        {
            if (contact.a != context.entity() && contact.b != context.entity())
                continue;
            const auto other = contact.a == context.entity() ? contact.b : contact.a;
            if (!registry.valid(other) || !registry.has<aegis::physics::RigidBody>(other) || registry.get<aegis::physics::RigidBody>(other).motionType != aegis::physics::MotionType::Dynamic)
                continue;
            if (auto played = aegis::audio::play(registry, context.entity()); !played)
                aegis::log::warn("ContactSound on '{}' can't play: {}", context.get<aegis::Name>().name, aegis::audio::describe(played.error()));
            m_played = true;
            return;
        }
    }

private:
    bool m_played{ false };
};
}

namespace examples
{
auto registerContactSound(aegis::reflect::TypeRegistry& types) -> std::expected<void, aegis::reflect::Error>
{
    return aegis::scripting::registerScript<ContactSound>(types, "ContactSound").and_then([](auto builder) { return builder.status(); });
}
}
