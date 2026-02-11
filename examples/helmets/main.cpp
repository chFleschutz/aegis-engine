#include <aegis/engine.h>
#include <aegis/scene/components.h>
#include <aegis/scene/description.h>
#include <aegis/scripting/script_base.h>

#include <aegis/math/random.h>

class ColorChanger : public Aegis::Scripting::ScriptBase
{
public:
	void update(float deltaSeconds) override
	{
		// Cycle through colors
		constexpr float speed = 0.5f;
		float time = static_cast<float>(glfwGetTime()) * speed;
		glm::vec3 color{
			(sin(time + 0.0f) + 1.0f) / 2.0f,
			(sin(time + 2.0f) + 1.0f) / 2.0f,
			(sin(time + 4.0f) + 1.0f) / 2.0f
		};

		auto& material = get<Aegis::Material>().instance;
		material->setParameter("albedo", color);
	}
};


/// @brief Scene with two helmets
class HelmetScene : public Aegis::Scene::Description
{
public:
	void initialize(Aegis::Scene::Scene& scene) override
	{
		using namespace Aegis;

		// SKYBOX
		auto& env = scene.environment().get<Environment>();
		env.skybox = Graphics::Texture::loadFromFile(ASSETS_DIR "Environments/KloppenheimSky.hdr");
		env.irradiance = Graphics::Texture::irradianceMap(env.skybox);
		env.prefiltered = Graphics::Texture::prefilteredMap(env.skybox);

		// LIGHTS
		scene.ambientLight().get<AmbientLight>().intensity = 1.0f;
		scene.directionalLight().get<DirectionalLight>().intensity = 1.0f;
		scene.directionalLight().get<Transform>().rotation = glm::radians(glm::vec3{ 60.0f, 0.0f, 135.0f });

		// CAMERA
		scene.mainCamera().get<Transform>() = Transform{
			.location = { -3.0f, -6.0f, 3.0f},
			.rotation = glm::radians(glm::vec3{ -8.0f, 0.0f, 335.0f })
		};
		
		// ENTITIES
		auto spheres = scene.load(ASSETS_DIR "MetalRoughSpheres/MetalRoughSpheres.gltf");
		spheres.get<Transform>().location = { 0.0f, 5.0f, 5.0f };

		auto damagedHelmet = scene.load(ASSETS_DIR "DamagedHelmet/DamagedHelmet.gltf");
		damagedHelmet.get<Transform>().location = { -2.0f, 0.0f, 2.0f };

		auto scifiHelmet = scene.load(ASSETS_DIR "SciFiHelmet/ScifiHelmet.gltf");
		scifiHelmet.get<Transform>().location = { 2.0f, 0.0f, 2.0f };

		auto plane = scene.load(ASSETS_DIR "Misc/plane.obj");
		plane.get<Transform>().scale = { 2.0f, 2.0f, 2.0f };
		plane.add<ColorChanger>();
		plane.add<DynamicTag>();
	}
};

auto main() -> int
{
	Aegis::Engine engine;
	engine.loadScene<HelmetScene>();
	engine.run();
}
