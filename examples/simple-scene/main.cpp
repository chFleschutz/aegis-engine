#include <aegis/core/assert.h>

import std;
import aegis.editor;
import aegis.engine;

/// @brief Example script to rotate the entity around the up axis
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

/// @brief Simple scene with a teapot, a plane and a ring of lights, loaded from Scenes/simple.scene
auto main(int argc, char** argv) -> int
{
    auto created = aegis::createEngine(argc, argv);
    if (!created)
        return created.error();
    auto& engine = **created;
    engine.pushLayer<aegis::editor::EditorLayer>();
    [[maybe_unused]] auto registered = aegis::scripting::registerScript<Rotator>(engine.types(), "Rotator").and_then([](auto builder) {
        return builder.field("speed", &Rotator::speed).status();
    });
    AGX_ASSERT_X(registered, "Failed to register the Rotator script");

    if (auto loaded = engine.loadScene(engine.assets().id("Scenes/simple.scene")); !loaded)
    {
        aegis::log::fatal("{}", aegis::describe(loaded.error()));
        return aegis::exitFailure;
    }
    return engine.run();
}
