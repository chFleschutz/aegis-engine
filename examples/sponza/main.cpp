import aegis.editor;
import aegis.engine;

class Sponza : public aegis::SceneDescription
{
public:
    void initialize(aegis::SceneContext& context) override
    {
        auto& [scene, types, assets] = context;
        aegis::addDefaults(context);
        using namespace aegis;

        auto& registry = scene.registry();

        // CAMERA
        registry.get<Transform>(scene.mainCamera()) = Transform{
            .location = { -9.75f, 1.2f, 5.25f },
            .rotation = glm::radians(glm::vec3{ -12.0f, 0.0f, 263.0f })
        };

        // SKYBOX
        registry.get<aegis::Sky>(registry.first<aegis::Sky>()).environment =
            assets.loadEnvironment(assets.id("Environments/KloppenheimSky.hdr"));

        // MODELS
        assets.instantiate(scene, assets.id("Sponza/Sponza.gltf"));

        // LIGHTS
        registry.get<AmbientLight>(scene.ambientLight()).intensity = 0.5f;
        registry.get<DirectionalLight>(scene.directionalLight()).intensity = 1.0f;
    }
};

auto main(int argc, char** argv) -> int
{
    auto engine = aegis::createEngine(argc, argv);
    if (!engine)
        return engine.error();
    (*engine)->pushLayer<aegis::editor::EditorLayer>();
    (*engine)->loadScene<Sponza>();
    return (*engine)->run();
}
