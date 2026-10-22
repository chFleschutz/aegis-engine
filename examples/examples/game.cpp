#include <aegis/launch/game_module.h>

import std;
import aegis.engine;

namespace
{
/// @brief Rotates its entity around the up axis
class Rotator : public aegis::scripting::Script
{
public:
    float speed{ 1.0f }; // Radians per second, a registered field

    void onUpdate(aegis::scripting::ScriptContext& context, float deltaSeconds) override
    {
        auto& transform = context.get<aegis::Transform>();
        transform.rotation *= glm::angleAxis(speed * deltaSeconds, aegis::math::world::UP);
    }
};

auto registerTypes(aegis::reflect::TypeRegistry& types) -> std::expected<void, aegis::reflect::Error>
{
    return aegis::scripting::registerScript<Rotator>(types, "Rotator").and_then([](auto builder) {
        return builder.field("speed", &Rotator::speed).status();
    });
}
}

AEGIS_GAME_MODULE({ .name = "Examples", .startupScene = "Scenes/simple.scene", .registerTypes = registerTypes })
