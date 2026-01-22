#include "aegis/engine.h"
#include "aegis/scene/description.h"
#include <aegis/scene/components.h>
#include <aegis/math/random.h>
#include <aegis/scene/system.h>

// 1. Crytek Sponza 
//	  - baseline standard small scene
class Sponza : public Aegis::Scene::Description
{
public:
	/// @brief All objects in a scene are created here
	void initialize(Aegis::Scene::Scene& scene) override
	{
		using namespace Aegis;

		auto& env = scene.environment().get<Environment>();
		env.skybox = Graphics::Texture::loadFromFile(ASSETS_DIR "Environments/KloppenheimSky.hdr");
		env.irradiance = Graphics::Texture::irradianceMap(env.skybox);
		env.prefiltered = Graphics::Texture::prefilteredMap(env.skybox);

		scene.ambientLight().get<AmbientLight>().intensity = 0.25f;
		scene.directionalLight().get<DirectionalLight>().intensity = 2.0f;

		scene.load(ASSETS_DIR "Sponza/Sponza.gltf");
		scene.mainCamera().get<Transform>() = Transform{
			.location = { -9.75f, 1.2f, 5.25f},
			.rotation = glm::radians(glm::vec3{ -12.0f, 0.0f, 263.0f })
		};
	}
};


// 2. Lumberyard Bistro 
//    - larger scene with a lot of objects and details
class Bistro : public Aegis::Scene::Description
{
public:
	/// @brief All objects in a scene are created here
	void initialize(Aegis::Scene::Scene& scene) override
	{
		using namespace Aegis;

		// Environment setup
		auto& env = scene.environment().get<Environment>();
		env.skybox = Graphics::Texture::loadFromFile(ASSETS_DIR "Environments/KloppenheimSky.hdr");
		env.irradiance = Graphics::Texture::irradianceMap(env.skybox);
		env.prefiltered = Graphics::Texture::prefilteredMap(env.skybox);

		scene.ambientLight().get<AmbientLight>().intensity = 0.25f;
		scene.directionalLight().get<DirectionalLight>().intensity = 2.0f;

		scene.load(ENGINE_DIR "temp/Bistro/bistro.gltf");
		scene.mainCamera().get<Transform>() = Transform{
			.location = { -24.5f, 2.75f, 5.25f},
			.rotation = glm::radians(glm::vec3{ -1.5f, 0.0f, -90.0f })
		};
	}
};


// 3. Large Instanced Scene (100k of the same fairly large objects, like trees, rocks, etc)
//    - test instancing on larger objects
class HighPolyHighObj : public Aegis::Scene::Description
{
public:
	int cameraMode = 0; // 0 = inside instances, 1 = outside instances
	HighPolyHighObj(int camMode) : cameraMode(camMode) {}

	/// @brief All objects in a scene are created here
	void initialize(Aegis::Scene::Scene& scene) override
	{
		using namespace Aegis;

		// Environment setup
		auto& env = scene.environment().get<Environment>();
		env.skybox = Graphics::Texture::loadFromFile(ASSETS_DIR "Environments/KloppenheimSky.hdr");
		env.irradiance = Graphics::Texture::irradianceMap(env.skybox);
		env.prefiltered = Graphics::Texture::prefilteredMap(env.skybox);

		scene.ambientLight().get<AmbientLight>().intensity = 0.25f;
		scene.directionalLight().get<DirectionalLight>().intensity = 2.0f;
		if (cameraMode == 0)
		{
			// Inside Instances (only parts visible)
			scene.mainCamera().get<Transform>() = Transform{
				.location = { 30.0f, -30.0f, 5.0f},
				.rotation = glm::radians(glm::vec3{ -20.0f, 0.0f, 45.0f })
			};
		}
		else
		{
			//Outside Instances (everything visible)
			scene.mainCamera().get<Transform>() = Transform{
				.location = { 150.0f, -150.0f, 100.0f},
				.rotation = glm::radians(glm::vec3{ -35.0f, 0.0f, 45.0f })
			};
		}

		auto scifiHelmet = scene.load(ASSETS_DIR "SciFiHelmet/ScifiHelmet.gltf");
		scifiHelmet.get<Transform>().location = { 2.0f, 0.0f, 2.0f };

		Scene::Entity meshEntity = scifiHelmet;
		std::shared_ptr<Graphics::StaticMesh> mesh;
		std::shared_ptr<Graphics::MaterialInstance> materialInstance;
		while (!meshEntity.has<Mesh, Material>())
		{
			meshEntity = meshEntity.get<Children>().last;
			AGX_ASSERT_X(meshEntity, "Failed to find mesh and material in SciFiHelmet scene");
		}
		mesh = meshEntity.get<Mesh>().staticMesh;
		materialInstance = meshEntity.get<Material>().instance;
		AGX_ASSERT(mesh && materialInstance);

		constexpr int instanceCount = 10'000;
		constexpr float boxSize = 100.0f;

		auto posDis = std::uniform_real_distribution<float>(-boxSize, boxSize);
		auto rotDis = std::uniform_real_distribution<float>(0.0f, 360.0f);
		auto scaleDis = std::uniform_real_distribution<float>(0.5f, 2.0f);
		for (int i = 0; i < instanceCount; i++)
		{
			auto instance = scene.createEntity("SciFiHelmetInstance");
			instance.add<Mesh>(mesh);
			instance.add<Material>(materialInstance);
			auto& transform = instance.get<Transform>();
			transform.location = {
				posDis(Random::generator()),
				posDis(Random::generator()),
				posDis(Random::generator()) / 2.0f
			};
			transform.rotation = glm::radians(glm::vec3{ 90.0f, 0.0f, rotDis(Random::generator()) });
			transform.scale = glm::vec3{ scaleDis(Random::generator()) };
		}
	}
};


// 4. Extreme Instanced Scene (1 million+ of the same small objects, like grass blades, cubes, etc)
//    - test limits of gpu driven rendering
class LowPolyHighObj : public Aegis::Scene::Description
{
public:
	int cameraMode = 0; // 0 = inside instances, 1 = outside instances
	LowPolyHighObj(int camMode) : cameraMode(camMode) {}

	/// @brief All objects in a scene are created here
	void initialize(Aegis::Scene::Scene& scene) override
	{
		using namespace Aegis;

		// Environment setup
		auto& env = scene.environment().get<Environment>();
		env.skybox = Graphics::Texture::loadFromFile(ASSETS_DIR "Environments/KloppenheimSky.hdr");
		env.irradiance = Graphics::Texture::irradianceMap(env.skybox);
		env.prefiltered = Graphics::Texture::prefilteredMap(env.skybox);

		scene.ambientLight().get<AmbientLight>().intensity = 0.25f;
		scene.directionalLight().get<DirectionalLight>().intensity = 2.0f;

		if (cameraMode == 0)
		{
			// Inside Instances (only parts visible)
			scene.mainCamera().get<Transform>() = Transform{
				.location = { 30.0f, -30.0f, 5.0f},
				.rotation = glm::radians(glm::vec3{ -20.0f, 0.0f, 45.0f })
			};
		}
		else
		{
			// Outside Instances (everything visible)
			scene.mainCamera().get<Transform>() = Transform{
				.location = { -500.0f, -500.0f, 330.0f},
				.rotation = glm::radians(glm::vec3{ -30.0f, 0.0f, -45.0f })
			};
		}

		auto cube = scene.load(ASSETS_DIR "Misc/cube.obj");
		auto& cubeMesh = cube.get<Mesh>().staticMesh;

		constexpr int materialCount = 10;
		auto pbrMatTemplate = Engine::assets().get<Graphics::MaterialTemplate>("default/PBR_template");
		std::vector<std::shared_ptr<Graphics::MaterialInstance>> cubeMats;
		for (int i = 0; i < materialCount; i++)
		{
			auto matInstance = Graphics::MaterialInstance::create(pbrMatTemplate);
			glm::vec3 color{
				Random::uniformFloat(0.0f, 1.0f),
				Random::uniformFloat(0.0f, 1.0f),
				Random::uniformFloat(0.0f, 1.0f)
			};
			matInstance->setParameter("albedo", color);
			matInstance->setParameter("metallic", Random::uniformFloat(0.0f, 1.0f));
			matInstance->setParameter("roughness", Random::uniformFloat(0.0f, 1.0f));
			cubeMats.emplace_back(matInstance);
		}

		constexpr int cubeCount = 1'000'000;
		constexpr float areaSize = 500.0f;

		std::uniform_real_distribution<float> posDis(-areaSize / 2.0f, areaSize / 2.0f);
		std::uniform_real_distribution<float> rotDis(0.0f, 360.0f);
		std::uniform_real_distribution<float> scaleDis(0.5f, 2.0f);

		for (int i = 0; i < cubeCount; i++)
		{
			glm::vec3 pos{
				posDis(Random::generator()),
				posDis(Random::generator()),
				posDis(Random::generator())
			};
			glm::quat rot = glm::radians(glm::vec3{
				rotDis(Random::generator()),
				rotDis(Random::generator()),
				rotDis(Random::generator())
				});
			glm::vec3 scale{ scaleDis(Random::generator()) };

			auto cubeInstance = scene.createEntity(std::format("Cube {}", i), pos, rot, scale);
			cubeInstance.add<Mesh>(cubeMesh);
			cubeInstance.add<Material>(cubeMats[i % materialCount]);
		}
	}
};


// 5. Dynamic Scene (many moving objects with dynamic tag)
//    - test upload bottleneck for dynamic objects

struct Rotatable
{
	float speed = 1.0f;
};

class RotationSystem : public Aegis::Scene::System
{
public:
	void onUpdate(float deltaSeconds, Aegis::Scene::Scene& scene) override
	{
		auto view = scene.registry().view<Aegis::Transform, Rotatable, Aegis::DynamicTag>();
		for (auto [entity, transform, rotatable] : view.each())
		{
			transform.rotation *= glm::angleAxis(rotatable.speed * deltaSeconds, Aegis::Math::World::UP);
		}
	}
};

class DynamicObjects : public Aegis::Scene::Description
{
public:
	/// @brief All objects in a scene are created here
	void initialize(Aegis::Scene::Scene& scene) override
	{
		using namespace Aegis;

		// Environment setup
		auto& env = scene.environment().get<Environment>();
		env.skybox = Graphics::Texture::loadFromFile(ASSETS_DIR "Environments/KloppenheimSky.hdr");
		env.irradiance = Graphics::Texture::irradianceMap(env.skybox);
		env.prefiltered = Graphics::Texture::prefilteredMap(env.skybox);

		scene.ambientLight().get<AmbientLight>().intensity = 0.25f;
		scene.directionalLight().get<DirectionalLight>().intensity = 2.0f;
		scene.mainCamera().get<Transform>() = Transform{
			.location = { -200.0f, -200.0f, 150.0f},
			.rotation = glm::radians(glm::vec3{ -32.0f, 0.0f, -45.0f })
		};

		scene.addSystem<RotationSystem>();

		auto cube = scene.load(ASSETS_DIR "Misc/cube.obj");
		auto& cubeMesh = cube.get<Mesh>().staticMesh;
		auto& cubeMat = cube.get<Material>().instance;
		cubeMat->setParameter("albedo", glm::vec3{ 0.8f, 0.1f, 0.1f });
		cubeMat->setParameter("metallic", 1.0f);
		cubeMat->setParameter("roughness", 0.5f);

		constexpr int cubeCount = 10'000; 
		constexpr float areaSize = 200.0f;
		std::uniform_real_distribution<float> posDis(-areaSize / 2.0f, areaSize / 2.0f);
		std::uniform_real_distribution<float> rotDis(0.0f, 360.0f);
		std::uniform_real_distribution<float> scaleDis(0.5f, 2.0f);
		for (int i = 0; i < cubeCount; i++)
		{
			glm::vec3 pos{
				posDis(Random::generator()),
				posDis(Random::generator()),
				posDis(Random::generator())
			};
			glm::quat rot = glm::radians(glm::vec3{
				rotDis(Random::generator()),
				rotDis(Random::generator()),
				rotDis(Random::generator())
				});
			glm::vec3 scale{ scaleDis(Random::generator()) };

			auto cubeInstance = scene.createEntity(std::format("Cube {}", i), pos, rot, scale);
			cubeInstance.add<Mesh>(cubeMesh);
			cubeInstance.add<Material>(cubeMat);
			cubeInstance.add<Rotatable>(glm::radians(Random::uniformFloat(10.0f, 90.0f)));
			cubeInstance.add<DynamicTag>(); // Mark dynamic, so its updated every frame
		}
	}
};


auto main() -> int
{
	Aegis::Engine engine;
	engine.loadScene<Sponza>();
	//engine.loadScene<Bistro>();
	//engine.loadScene<HighPolyHighObj>(0);
	//engine.loadScene<HighPolyHighObj>(1);
	//engine.loadScene<LowPolyHighObj>(0);
	//engine.loadScene<LowPolyHighObj>(1);
	//engine.loadScene<DynamicObjects>();
	engine.run();
}
