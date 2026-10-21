#include <aegis/core/assert.h>

import std;
import aegis.editor;
import aegis.engine;

class ColorChanger : public aegis::scripting::Script
{
public:
    float speed{ 0.5f }; // A registered field

    void onUpdate(aegis::scripting::ScriptContext& context, float deltaSeconds) override
    {
        // Cycle through colors
        m_time += deltaSeconds * speed;
        glm::vec3 color{
            (std::sin(m_time + 0.0f) + 1.0f) / 2.0f,
            (std::sin(m_time + 2.0f) + 1.0f) / 2.0f,
            (std::sin(m_time + 4.0f) + 1.0f) / 2.0f
        };

        auto& material = context.get<aegis::MeshRenderer>().material;
        [[maybe_unused]] auto set = material->setParameter(aegis::assets::pbr::albedo, color);
        AGX_ASSERT_X(set, "Failed to set the material's albedo");
    }

private:
    float m_time = 0.0f;
};

/// @brief Scene with two helmets
class HelmetScene : public aegis::SceneDescription
{
public:
    void initialize(aegis::SceneContext& context) override
    {
        auto& [scene, types, assets] = context;
        aegis::addDefaults(context);
        using namespace aegis;

        auto& registry = scene.registry();

        // SKYBOX
        registry.get<aegis::Sky>(registry.first<aegis::Sky>()).environment =
            assets.loadEnvironment(assets.id("Environments/KloppenheimSky.hdr"));

        // LIGHTS
        registry.get<AmbientLight>(scene.ambientLight()).intensity = 1.0f;
        registry.get<DirectionalLight>(scene.directionalLight()).intensity = 1.0f;
        registry.get<Transform>(scene.directionalLight()).rotation = glm::radians(glm::vec3{ 60.0f, 0.0f, 135.0f });

        // CAMERA
        registry.get<Transform>(scene.mainCamera()) = Transform{
            .location = { -3.0f, -6.0f, 3.0f },
            .rotation = glm::radians(glm::vec3{ -8.0f, 0.0f, 335.0f })
        };

        // ENTITIES
        auto spheres = aegis::instantiate(context, assets.id("MetalRoughSpheres/MetalRoughSpheres.gltf"));
        registry.get<Transform>(spheres).location = { 0.0f, 5.0f, 5.0f };

        auto damagedHelmet = aegis::instantiate(context, assets.id("DamagedHelmet/DamagedHelmet.gltf"));
        registry.get<Transform>(damagedHelmet).location = { -2.0f, 0.0f, 2.0f };

        auto scifiHelmet = aegis::instantiate(context, assets.id("SciFiHelmet/SciFiHelmet.gltf"));
        registry.get<Transform>(scifiHelmet).location = { 2.0f, 0.0f, 2.0f };

        auto plane = aegis::instantiate(context, assets.id("Misc/plane.obj"));
        registry.get<Transform>(plane).scale = { 2.0f, 2.0f, 2.0f };
        const auto planeMesh = registry.get<Children>(plane).first; // The model's root, which draws the OBJ's mesh
        registry.add<DynamicTag>(planeMesh);
        // The ColorChanger recolors its material every frame: give the plane its own copy of the shared one.
        auto& planeMaterial = registry.get<aegis::MeshRenderer>(planeMesh).material;
        planeMaterial = assets.copyMaterial(planeMaterial);
        [[maybe_unused]] auto changer = scripting::attachScript(registry, types, planeMesh, "ColorChanger");
        AGX_ASSERT_X(changer, "The ColorChanger script is not registered");
    }
};

auto main(int argc, char** argv) -> int
{
    auto created = aegis::createEngine(argc, argv);
    if (!created)
        return created.error();
    auto& engine = **created;
    engine.pushLayer<aegis::editor::EditorLayer>();
    [[maybe_unused]] auto registered = aegis::scripting::registerScript<ColorChanger>(engine.types(), "ColorChanger").and_then([](auto builder) {
        return builder.field("speed", &ColorChanger::speed).status();
    });
    AGX_ASSERT_X(registered, "Failed to register the ColorChanger script");
    engine.loadScene<HelmetScene>();
    return engine.run();
}
