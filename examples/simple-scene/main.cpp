import std;
import aegis.engine;

/// @brief Example script to rotate the entity around the up axis
class Rotator : public aegis::scripting::ScriptBase
{
protected:
    void update(float deltaSeconds) override
    {
        auto& transform = get<aegis::Transform>();
        transform.rotation *= glm::angleAxis(deltaSeconds, aegis::math::world::UP);
    }
};

/// @brief Simple scene with a teapot, a plane and a bunch of lights
class SimpleScene : public aegis::SceneDescription
{
public:
    void initialize(aegis::scene::Scene& scene, aegis::scripting::ScriptManager& scripts) override
    {
        using namespace aegis;

        // MATERIALS
        auto paintingTexture = Engine::loadTexture(core::ASSETS_DIR / "Misc/painting.png", aegis::rhi::Format::RGBA8_SRGB);
        auto metalTexture = Engine::loadTexture(core::ASSETS_DIR / "Misc/brushed-metal.png", aegis::rhi::Format::RGBA8_SRGB);

        auto pbrMatTemplate = Engine::defaults().pbrTemplate;
        auto paintingMat = Engine::createMaterial(pbrMatTemplate);
        paintingMat->setParameter("albedoMap", paintingTexture);

        auto metalMat = Engine::createMaterial(pbrMatTemplate);
        metalMat->setParameter("albedoMap", metalTexture);
        metalMat->setParameter("metallic", 1.0f);

        // ENTITIES
        auto& registry = scene.registry();

        auto floorPlane = Engine::loadModel(core::ASSETS_DIR / "Misc/plane.obj");
        registry.get<Transform>(floorPlane).scale = glm::vec3{ 2.0f, 2.0f, 2.0f };
        registry.get<aegis::assets::Material>(floorPlane).instance = paintingMat;

        auto teapot = Engine::loadModel(core::ASSETS_DIR / "Misc/teapot.obj");
        registry.get<Transform>(teapot).scale = glm::vec3{ 2.0f, 2.0f, 2.0f };
        registry.get<aegis::assets::Material>(teapot).instance = metalMat;
        scripts.addScript<Rotator>(teapot);

        // LIGHTS
        constexpr int lightCount = 64;
        constexpr float lightRadius = 10.0f;
        for (int i = 0; i < lightCount; i++)
        {
            float x = lightRadius * std::cos(glm::radians(360.0f / lightCount * i));
            float y = lightRadius * std::sin(glm::radians(360.0f / lightCount * i));
            auto light = registry.create("Light " + std::to_string(i), { x, y, 3.0f });

            float r = math::Random::uniformFloat(0.0f, 1.0f);
            float g = math::Random::uniformFloat(0.0f, 1.0f);
            float b = math::Random::uniformFloat(0.0f, 1.0f);
            registry.add<PointLight>(light, glm::vec3{ r, g, b }, 200.0f);
        }
    }
};

auto main() -> int
{
    aegis::Engine engine;
    engine.loadScene<SimpleScene>();
    engine.run();
}
