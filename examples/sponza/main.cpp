import aegis.engine;

class Sponza : public aegis::SceneDescription
{
public:
    void initialize(aegis::SceneContext& context) override
    {
        auto& [scene, scripts, assets] = context;
        using namespace aegis;

        auto& registry = scene.registry();

        // CAMERA
        registry.get<Transform>(scene.mainCamera()) = Transform{
            .location = { -9.75f, 1.2f, 5.25f },
            .rotation = glm::radians(glm::vec3{ -12.0f, 0.0f, 263.0f })
        };

        // SKYBOX
        registry.get<aegis::assets::Environment>(scene.environment()) =
            assets.loadEnvironment(paths::assets() / "Environments/KloppenheimSky.hdr");

        // MODELS
        assets.loadModel(scene, paths::assets() / "Sponza/Sponza.gltf");

        // LIGHTS
        registry.get<AmbientLight>(scene.ambientLight()).intensity = 0.5f;
        registry.get<DirectionalLight>(scene.directionalLight()).intensity = 1.0f;
    }
};

auto main() -> int
{
    aegis::Engine engine;
    engine.loadScene<Sponza>();
    engine.run();
}
