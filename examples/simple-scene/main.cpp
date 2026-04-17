#include <cmath>
#include <string>

import Aegis.Engine;

/// @brief Example script to rotate the entity around the up axis
class Rotator : public Aegis::Scripting::ScriptBase
{
protected:
	void update(float deltaSeconds) override
	{
		auto& transform = get<Aegis::Transform>();
		transform.rotation *= glm::angleAxis(deltaSeconds, Aegis::Math::World::UP);
	}
};

/// @brief Simple scene with a teapot, a plane and a bunch of lights
class SimpleScene : public Aegis::SceneDescription
{
public:
	void initialize(Aegis::Scene::Scene& scene, Aegis::Scripting::ScriptManager& scripts) override
	{
		using namespace Aegis;

		// MATERIALS
		auto paintingTexture = Graphics::Texture::loadFromFile(Core::ASSETS_DIR / "Misc/painting.png", VK_FORMAT_R8G8B8A8_SRGB);
		auto metalTexture = Graphics::Texture::loadFromFile(Core::ASSETS_DIR / "Misc/brushed-metal.png", VK_FORMAT_R8G8B8A8_SRGB);
		
		auto pbrMatTemplate = Core::AssetManager::instance().get<Graphics::MaterialTemplate>("default/PBR_template");
		auto paintingMat = Graphics::MaterialInstance::create(pbrMatTemplate);
		paintingMat->setParameter("albedoMap", paintingTexture);

		auto metalMat = Graphics::MaterialInstance::create(pbrMatTemplate);
		metalMat->setParameter("albedoMap", metalTexture);
		metalMat->setParameter("metallic", 1.0f);
		
		// ENTITIES
		auto& registry = scene.registry();

		auto floorPlane = Graphics::Loader::load(registry, Core::ASSETS_DIR / "Misc/plane.obj");
		registry.get<Transform>(floorPlane).scale = glm::vec3{ 2.0f, 2.0f, 2.0f };
		registry.get<Graphics::Material>(floorPlane).instance = paintingMat;

		auto teapot = Graphics::Loader::load(registry, Core::ASSETS_DIR / "Misc/teapot.obj");
		registry.get<Transform>(teapot).scale = glm::vec3{ 2.0f, 2.0f, 2.0f };
		registry.get<Graphics::Material>(teapot).instance = metalMat;
		scripts.addScript<Rotator>(teapot);

		// LIGHTS
		constexpr int lightCount = 64;
		constexpr float lightRadius = 10.0f;
		for (int i = 0; i < lightCount; i++)
		{
			float x = lightRadius * cos(glm::radians(360.0f / lightCount * i));
			float y = lightRadius * sin(glm::radians(360.0f / lightCount * i));
			auto light = registry.create("Light " + std::to_string(i), { x, y, 3.0f });

			float r = Math::Random::uniformFloat(0.0f, 1.0f);
			float g = Math::Random::uniformFloat(0.0f, 1.0f);
			float b = Math::Random::uniformFloat(0.0f, 1.0f);
			registry.add<PointLight>(light, glm::vec3{ r, g, b }, 200.0f);
		}
	}
};

auto main() -> int
{
	Aegis::Engine engine;
	engine.loadScene<SimpleScene>();
	engine.run();
}
