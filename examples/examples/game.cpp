#include <aegis/launch/game_module.h>

import std;
import aegis.engine;

namespace examples
{
auto registerSpawner(aegis::reflect::TypeRegistry& types) -> std::expected<void, aegis::reflect::Error>; // spawner.cpp
}

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

/// @brief Cycles the albedo of its entity's material through the colors
/// @note Changes the material for everything that uses it: give the entity a material of its own
class ColorChanger : public aegis::scripting::Script
{
public:
    float speed{ 0.5f }; // A registered field

    void onUpdate(aegis::scripting::ScriptContext& context, float deltaSeconds) override
    {
        m_time += deltaSeconds * speed;
        const glm::vec3 color{
            (std::sin(m_time + 0.0f) + 1.0f) / 2.0f,
            (std::sin(m_time + 2.0f) + 1.0f) / 2.0f,
            (std::sin(m_time + 4.0f) + 1.0f) / 2.0f
        };

        auto& material = context.get<aegis::MeshRenderer>().material;
        if (auto set = material->setParameter(aegis::assets::pbr::albedo, color); !set && !m_warned)
        {
            aegis::log::warn("ColorChanger can't set the albedo of its material");
            m_warned = true;
        }
    }

private:
    float m_time{ 0.0f };
    bool m_warned{ false };
};

auto registerTypes(aegis::reflect::TypeRegistry& types) -> std::expected<void, aegis::reflect::Error>
{
    return aegis::scripting::registerScript<Rotator>(types, "Rotator")
        .and_then([](auto builder) { return builder.field("speed", &Rotator::speed).status(); })
        .and_then([&types] { return aegis::scripting::registerScript<ColorChanger>(types, "ColorChanger"); })
        .and_then([](auto builder) { return builder.field("speed", &ColorChanger::speed).status(); })
        .and_then([&types] { return examples::registerSpawner(types); });
}
}

AEGIS_GAME_MODULE({ .name = "Examples", .startupScene = "Scenes/simple.scene", .registerTypes = registerTypes })
