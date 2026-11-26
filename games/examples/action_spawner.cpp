import std;
import aegis.game;
import aegis.serialize; // The prefab is a scene asset

namespace math = aegis::math;

namespace
{
/// @brief Spawns its prefab, a scene, each time the input action 'action' is pressed: a root entity at the spawner's
///        location plus 'offset' holds each copy
class ActionSpawner : public aegis::scripting::Script
{
public:
    aegis::assets::AssetRef<aegis::serialize::Scene> prefab; // A registered field
    std::string action{ "Spawn" };                           // A registered field
    math::Float3 offset{ 0.0f };                             // From the spawner's location, a registered field

    void onUpdate(aegis::scripting::ScriptContext& context, float) override
    {
        if (!context.input().action(action).pressed())
            return;
        const auto location = context.get<aegis::GlobalTransform>().location() + offset;
        if (context.spawn(prefab, {}, aegis::Transform{ .location = location }) == aegis::StableId{})
            aegis::log::warn("ActionSpawner on '{}' can't spawn in this world", context.get<aegis::Name>().name);
    }
};
}

namespace examples
{
auto registerActionSpawner(aegis::reflect::TypeRegistry& types) -> std::expected<void, aegis::reflect::Error>
{
    return aegis::scripting::registerScript<ActionSpawner>(types, "ActionSpawner").and_then([](auto builder) {
        return builder.field("prefab", &ActionSpawner::prefab).field("action", &ActionSpawner::action).field("offset", &ActionSpawner::offset).status();
    });
}
}
