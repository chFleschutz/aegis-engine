import std;
import aegis.engine;

class ColorChanger : public aegis::scripting::ScriptBase
{
public:
    void update(float deltaSeconds) override
    {
        // Cycle through colors
        constexpr float speed = 0.5f;
        m_time += deltaSeconds * speed;
        glm::vec3 color{
            (std::sin(m_time + 0.0f) + 1.0f) / 2.0f,
            (std::sin(m_time + 2.0f) + 1.0f) / 2.0f,
            (std::sin(m_time + 4.0f) + 1.0f) / 2.0f
        };

        auto& material = get<aegis::assets::Material>().instance;
        material->setParameter("albedo", color);
    }

private:
    float m_time = 0.0f;
};

/// @brief Scene with two helmets
class HelmetScene : public aegis::SceneDescription
{
public:
    void initialize(aegis::scene::Scene& scene, aegis::scripting::ScriptManager& scripts) override
    {
        using namespace aegis;

        auto& registry = scene.registry();

        // SKYBOX
        registry.get<aegis::assets::Environment>(scene.environment()) =
            Engine::loadEnvironment(core::ASSETS_DIR / "Environments/KloppenheimSky.hdr");

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
        auto spheres = Engine::loadModel(core::ASSETS_DIR / "MetalRoughSpheres/MetalRoughSpheres.gltf");
        registry.get<Transform>(spheres).location = { 0.0f, 5.0f, 5.0f };

        auto damagedHelmet = Engine::loadModel(core::ASSETS_DIR / "DamagedHelmet/DamagedHelmet.gltf");
        registry.get<Transform>(damagedHelmet).location = { -2.0f, 0.0f, 2.0f };

        auto scifiHelmet = Engine::loadModel(core::ASSETS_DIR / "SciFiHelmet/SciFiHelmet.gltf");
        registry.get<Transform>(scifiHelmet).location = { 2.0f, 0.0f, 2.0f };

        auto plane = Engine::loadModel(core::ASSETS_DIR / "Misc/plane.obj");
        registry.get<Transform>(plane).scale = { 2.0f, 2.0f, 2.0f };
        registry.add<DynamicTag>(plane);
        scripts.addScript<ColorChanger>(plane);
    }
};

auto main() -> int
{
    aegis::Engine engine;
    engine.loadScene<HelmetScene>();
    engine.run();
}
