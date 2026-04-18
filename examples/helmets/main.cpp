#include <cmath>

import Aegis.Engine;

class ColorChanger : public Aegis::Scripting::ScriptBase
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

		auto& material = get<Aegis::Graphics::Material>().instance;
		material->setParameter("albedo", color);
	}

private:
	float m_time = 0.0f;
};


/// @brief Scene with two helmets
class HelmetScene : public Aegis::SceneDescription
{
public:
	void initialize(Aegis::Scene::Scene& scene, Aegis::Scripting::ScriptManager& scripts) override
	{
		using namespace Aegis;

		auto& registry = scene.registry();

		// SKYBOX
		auto& env = registry.get<Graphics::Environment>(scene.environment());
		env.skybox = Graphics::Texture::loadFromFile(Core::ASSETS_DIR / "Environments/KloppenheimSky.hdr");
		env.irradiance = Graphics::Texture::irradianceMap(env.skybox);
		env.prefiltered = Graphics::Texture::prefilteredMap(env.skybox);

		// LIGHTS
		registry.get<AmbientLight>(scene.ambientLight()).intensity = 1.0f;
		registry.get<DirectionalLight>(scene.directionalLight()).intensity = 1.0f;
		registry.get<Transform>(scene.directionalLight()).rotation = glm::radians(glm::vec3{ 60.0f, 0.0f, 135.0f });

		// CAMERA
		registry.get<Transform>(scene.mainCamera()) = Transform{
			.location = { -3.0f, -6.0f, 3.0f},
			.rotation = glm::radians(glm::vec3{ -8.0f, 0.0f, 335.0f })
		};
		
		// ENTITIES
		auto spheres = Graphics::Loader::load(registry, Core::ASSETS_DIR / "MetalRoughSpheres/MetalRoughSpheres.gltf");
		registry.get<Transform>(spheres).location = { 0.0f, 5.0f, 5.0f };

		auto damagedHelmet = Graphics::Loader::load(registry, Core::ASSETS_DIR / "DamagedHelmet/DamagedHelmet.gltf");
		registry.get<Transform>(damagedHelmet).location = { -2.0f, 0.0f, 2.0f };

		auto scifiHelmet = Graphics::Loader::load(registry, Core::ASSETS_DIR / "SciFiHelmet/SciFiHelmet.gltf");
		registry.get<Transform>(scifiHelmet).location = { 2.0f, 0.0f, 2.0f };

		auto plane = Graphics::Loader::load(registry, Core::ASSETS_DIR / "Misc/plane.obj");
		registry.get<Transform>(plane).scale = { 2.0f, 2.0f, 2.0f };
		registry.add<DynamicTag>(plane);
		scripts.addScript<ColorChanger>(plane);
	}
};

auto main() -> int
{
	Aegis::Engine engine;
	engine.loadScene<HelmetScene>();
	engine.run();
}
