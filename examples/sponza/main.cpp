import Aegis.Engine;

class Sponza : public Aegis::SceneDescription
{
public:
	void initialize(Aegis::Scene::Scene& scene, Aegis::Scripting::ScriptManager& scripts) override
	{
		using namespace Aegis;

		auto& registry = scene.registry();

		// CAMERA
		registry.get<Transform>(scene.mainCamera()) = Transform{
			.location = { -9.75f, 1.2f, 5.25f},
			.rotation = glm::radians(glm::vec3{ -12.0f, 0.0f, 263.0f })
		};

		// SKYBOX
		auto& env = registry.get<Graphics::Environment>(scene.environment());
		env.skybox = Graphics::Texture::loadFromFile(Core::ASSETS_DIR / "Environments/KloppenheimSky.hdr");
		env.irradiance = Graphics::Texture::irradianceMap(env.skybox);
		env.prefiltered = Graphics::Texture::prefilteredMap(env.skybox);

		// MODELS
		Graphics::Loader::load(registry, Core::ASSETS_DIR / "Sponza/Sponza.gltf");

		// LIGHTS
		registry.get<AmbientLight>(scene.ambientLight()).intensity = 0.5f;
		registry.get<DirectionalLight>(scene.directionalLight()).intensity = 1.0f;
	}
};

auto main() -> int
{
	Aegis::Engine engine;
	engine.loadScene<Sponza>();
	engine.run();
}
