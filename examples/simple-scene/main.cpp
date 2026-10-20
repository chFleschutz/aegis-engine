#include <aegis/core/assert.h>

import std;
import aegis.engine;
import aegis.rhi; // rhi::Format for loadTexture

namespace
{
std::mt19937 rng{ std::random_device{}() };

auto uniformFloat(float min, float max) -> float
{
    return std::uniform_real_distribution<float>(min, max)(rng);
}
}

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

/// @brief Simple scene with a teapot, a plane and a bunch of lights
class SimpleScene : public aegis::SceneDescription
{
public:
    void initialize(aegis::SceneContext& context) override
    {
        auto& [scene, types, assets] = context;
        aegis::addDefaults(context);
        using namespace aegis;

        // MATERIALS
        auto paintingTexture = assets.loadTexture(paths::assets() / "Misc/painting.png", aegis::rhi::Format::RGBA8_SRGB);
        auto metalTexture = assets.loadTexture(paths::assets() / "Misc/brushed-metal.png", aegis::rhi::Format::RGBA8_SRGB);

        auto pbrMatTemplate = assets.defaults().pbrTemplate;
        auto paintingMat = assets.createMaterial(pbrMatTemplate);
        [[maybe_unused]] auto paintingSet = paintingMat->setParameter(assets::pbr::albedoMap, paintingTexture);
        AGX_ASSERT_X(paintingSet, "Failed to set the painting material's parameters");

        auto metalMat = assets.createMaterial(pbrMatTemplate);
        [[maybe_unused]] auto metalSet = metalMat->setParameter(assets::pbr::albedoMap, metalTexture).and_then([&] {
            return metalMat->setParameter(assets::pbr::metallic, 1.0f);
        });
        AGX_ASSERT_X(metalSet, "Failed to set the metal material's parameters");

        // ENTITIES
        auto& registry = scene.registry();

        auto floorPlane = assets.loadModel(scene, paths::assets() / "Misc/plane.obj");
        registry.get<Transform>(floorPlane).scale = glm::vec3{ 2.0f, 2.0f, 2.0f };
        registry.get<aegis::assets::Material>(floorPlane).instance = paintingMat;

        auto teapot = assets.loadModel(scene, paths::assets() / "Misc/teapot.obj");
        registry.get<Transform>(teapot).scale = glm::vec3{ 2.0f, 2.0f, 2.0f };
        registry.get<aegis::assets::Material>(teapot).instance = metalMat;
        registry.add<DynamicTag>(teapot); // The Rotator moves it every frame
        [[maybe_unused]] auto rotator = scripting::attachScript(registry, types, teapot, "Rotator");
        AGX_ASSERT_X(rotator, "The Rotator script is not registered");

        // LIGHTS
        constexpr int lightCount = 64;
        constexpr float lightRadius = 10.0f;
        for (int i = 0; i < lightCount; i++)
        {
            float x = lightRadius * std::cos(glm::radians(360.0f / lightCount * i));
            float y = lightRadius * std::sin(glm::radians(360.0f / lightCount * i));
            auto light = registry.create("Light " + std::to_string(i), { x, y, 3.0f });

            float r = uniformFloat(0.0f, 1.0f);
            float g = uniformFloat(0.0f, 1.0f);
            float b = uniformFloat(0.0f, 1.0f);
            registry.add<PointLight>(light, glm::vec3{ r, g, b }, 200.0f);
        }
    }
};

auto main() -> int
{
    aegis::Engine engine;
    [[maybe_unused]] auto registered = aegis::scripting::registerScript<Rotator>(engine.types(), "Rotator").and_then([](auto builder) {
        return builder.field("speed", &Rotator::speed).status();
    });
    AGX_ASSERT_X(registered, "Failed to register the Rotator script");
    engine.loadScene<SimpleScene>();
    engine.run();
}
