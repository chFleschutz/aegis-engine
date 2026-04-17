#include <aegis/core/assert.h>

#include <random>
#include <vector>
#include <memory>
#include <format>
#include <filesystem>

import Aegis.Engine;

// 1. Crytek Sponza 
//	  - baseline standard small scene
class Sponza : public Aegis::SceneDescription
{
public:
	/// @brief All objects in a scene are created here
	void initialize(Aegis::Scene::Scene& scene, Aegis::Scripting::ScriptManager& scripts) override
	{
		using namespace Aegis;
		auto& registry = scene.registry();

		auto& env = registry.get<Graphics::Environment>(scene.environment());
		env.skybox = Graphics::Texture::loadFromFile(Core::ASSETS_DIR / "Environments/KloppenheimSky.hdr");
		env.irradiance = Graphics::Texture::irradianceMap(env.skybox);
		env.prefiltered = Graphics::Texture::prefilteredMap(env.skybox);

		registry.get<AmbientLight>(scene.ambientLight()).intensity = 0.25f;
		registry.get<DirectionalLight>(scene.directionalLight()).intensity = 2.0f;

		Graphics::Loader::load(registry, Core::ASSETS_DIR / "Sponza/Sponza.gltf");
		registry.get<Transform>(scene.mainCamera()) = Transform{
			.location = { -9.75f, 1.2f, 5.25f},
			.rotation = glm::radians(glm::vec3{ -12.0f, 0.0f, 263.0f })
		};
	}
};


// 2. Lumberyard Bistro 
//    - larger scene with a lot of objects and details

// NOTE: The Bistro scene is very large (~1.2 GB) and is not included in the repository.
// It is originally available from NVIDIA's Orca collection as fbx: https://developer.nvidia.com/orca/amazon-lumberyard-bistro
// A gltf version of the scene can be found here: https://github.com/zeux/niagara_bistro
class Bistro : public Aegis::SceneDescription
{
public:
	/// @brief All objects in a scene are created here
	void initialize(Aegis::Scene::Scene& scene, Aegis::Scripting::ScriptManager& scripts) override
	{
		using namespace Aegis;
		auto& registry = scene.registry();

		// Environment setup
		auto& env = registry.get<Graphics::Environment>(scene.environment());
		env.skybox = Graphics::Texture::loadFromFile(Core::ASSETS_DIR / "Environments/KloppenheimSky.hdr");
		env.irradiance = Graphics::Texture::irradianceMap(env.skybox);
		env.prefiltered = Graphics::Texture::prefilteredMap(env.skybox);

		registry.get<AmbientLight>(scene.ambientLight()).intensity = 0.25f;
		registry.get<DirectionalLight>(scene.directionalLight()).intensity = 2.0f;

		std::filesystem::path bistroPath = Core::ENGINE_DIR / "temp/Bistro/bistro.gltf";
		AGX_ASSERT_X(std::filesystem::exists(bistroPath), 
			"Bistro scene not found! Please download the scene from 'https://github.com/zeux/niagara_bistro' and place it in the 'temp/Bistro' folder.");

		Graphics::Loader::load(registry, bistroPath);
		registry.get<Transform>(scene.mainCamera()) = Transform{
			.location = { -24.5f, 2.75f, 5.25f},
			.rotation = glm::radians(glm::vec3{ -1.5f, 0.0f, -90.0f })
		};
	}
};


// 3. Large Instanced Scene (100k of the same fairly large objects, like trees, rocks, etc)
//    - test instancing on larger objects
class HighPolyHighObj : public Aegis::SceneDescription
{
public:
	int cameraMode = 0; // 0 = inside instances, 1 = outside instances
	HighPolyHighObj(int camMode) : cameraMode(camMode) {}

	/// @brief All objects in a scene are created here
	void initialize(Aegis::Scene::Scene& scene, Aegis::Scripting::ScriptManager& scripts) override
	{
		using namespace Aegis;
		auto& registry = scene.registry();

		// Environment setup
		auto& env = registry.get<Graphics::Environment>(scene.environment());
		env.skybox = Graphics::Texture::loadFromFile(Core::ASSETS_DIR / "Environments/KloppenheimSky.hdr");
		env.irradiance = Graphics::Texture::irradianceMap(env.skybox);
		env.prefiltered = Graphics::Texture::prefilteredMap(env.skybox);

		registry.get<AmbientLight>(scene.ambientLight()).intensity = 0.25f;
		registry.get<DirectionalLight>(scene.directionalLight()).intensity = 2.0f;
		if (cameraMode == 0)
		{
			// Inside Instances (only parts visible)
			registry.get<Transform>(scene.mainCamera()) = Transform{
				.location = { 30.0f, -30.0f, 5.0f},
				.rotation = glm::radians(glm::vec3{ -20.0f, 0.0f, 45.0f })
			};
		}
		else
		{
			//Outside Instances (everything visible)
			registry.get<Transform>(scene.mainCamera()) = Transform{
				.location = { 150.0f, -150.0f, 100.0f},
				.rotation = glm::radians(glm::vec3{ -35.0f, 0.0f, 45.0f })
			};
		}

		auto scifiHelmet = Graphics::Loader::load(registry, Core::ASSETS_DIR / "SciFiHelmet/ScifiHelmet.gltf");
		registry.get<Transform>(scifiHelmet).location = { 2.0f, 0.0f, 2.0f };

		Scene::Entity meshEntity = scifiHelmet;
		std::shared_ptr<Graphics::StaticMesh> mesh;
		std::shared_ptr<Graphics::MaterialInstance> materialInstance;
		while (!registry.has<Graphics::Mesh, Graphics::Material>(meshEntity))
		{
			meshEntity = registry.get<Children>(meshEntity).last;
			AGX_ASSERT_X(meshEntity, "Failed to find mesh and material in SciFiHelmet scene");
		}
		mesh = registry.get<Graphics::Mesh>(meshEntity).staticMesh;
		materialInstance = registry.get<Graphics::Material>(meshEntity).instance;
		AGX_ASSERT(mesh && materialInstance);

		constexpr int instanceCount = 10'000;
		constexpr float boxSize = 100.0f;

		auto posDis = std::uniform_real_distribution<float>(-boxSize, boxSize);
		auto rotDis = std::uniform_real_distribution<float>(0.0f, 360.0f);
		auto scaleDis = std::uniform_real_distribution<float>(0.5f, 2.0f);
		for (int i = 0; i < instanceCount; i++)
		{
			auto instance = registry.create("SciFiHelmetInstance");
			registry.add<Graphics::Mesh>(instance, mesh);
			registry.add<Graphics::Material>(instance, materialInstance);
			auto& transform = registry.get<Transform>(instance);
			transform.location = {
				posDis(Math::Random::generator()),
				posDis(Math::Random::generator()),
				posDis(Math::Random::generator()) / 2.0f
			};
			transform.rotation = glm::radians(glm::vec3{ 90.0f, 0.0f, rotDis(Math::Random::generator()) });
			transform.scale = glm::vec3{ scaleDis(Math::Random::generator()) };
		}
	}
};


// 4. Extreme Instanced Scene (1 million+ of the same small objects, like grass blades, cubes, etc)
//    - test limits of gpu driven rendering
class LowPolyHighObj : public Aegis::SceneDescription
{
public:
	int cameraMode = 0; // 0 = inside instances, 1 = outside instances
	LowPolyHighObj(int camMode) : cameraMode(camMode) {}

	/// @brief All objects in a scene are created here
	void initialize(Aegis::Scene::Scene& scene, Aegis::Scripting::ScriptManager& scripts) override
	{
		using namespace Aegis;
		auto& registry = scene.registry();

		// Environment setup
		auto& env = registry.get<Graphics::Environment>(scene.environment());
		env.skybox = Graphics::Texture::loadFromFile(Core::ASSETS_DIR / "Environments/KloppenheimSky.hdr");
		env.irradiance = Graphics::Texture::irradianceMap(env.skybox);
		env.prefiltered = Graphics::Texture::prefilteredMap(env.skybox);

		registry.get<AmbientLight>(scene.ambientLight()).intensity = 0.25f;
		registry.get<DirectionalLight>(scene.directionalLight()).intensity = 2.0f;

		if (cameraMode == 0)
		{
			// Inside Instances (only parts visible)
			registry.get<Transform>(scene.mainCamera()) = Transform{
				.location = { 30.0f, -30.0f, 5.0f},
				.rotation = glm::radians(glm::vec3{ -20.0f, 0.0f, 45.0f })
			};
		}
		else
		{
			// Outside Instances (everything visible)
			registry.get<Transform>(scene.mainCamera()) = Transform{
				.location = { -500.0f, -500.0f, 330.0f},
				.rotation = glm::radians(glm::vec3{ -30.0f, 0.0f, -45.0f })
			};
		}

		auto cube = Graphics::Loader::load(registry, Core::ASSETS_DIR / "Misc/cube.obj");
		auto& cubeMesh = registry.get<Graphics::Mesh>(cube).staticMesh;

		constexpr int materialCount = 10;
		auto pbrMatTemplate = Core::AssetManager::instance().get<Graphics::MaterialTemplate>("default/PBR_template");
		std::vector<std::shared_ptr<Graphics::MaterialInstance>> cubeMats;
		for (int i = 0; i < materialCount; i++)
		{
			auto matInstance = Graphics::MaterialInstance::create(pbrMatTemplate);
			glm::vec3 color{
				Math::Random::uniformFloat(0.0f, 1.0f),
				Math::Random::uniformFloat(0.0f, 1.0f),
				Math::Random::uniformFloat(0.0f, 1.0f)
			};
			matInstance->setParameter("albedo", color);
			matInstance->setParameter("metallic", Math::Random::uniformFloat(0.0f, 1.0f));
			matInstance->setParameter("roughness", Math::Random::uniformFloat(0.0f, 1.0f));
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
				posDis(Math::Random::generator()),
				posDis(Math::Random::generator()),
				posDis(Math::Random::generator())
			};
			glm::quat rot = glm::radians(glm::vec3{
				rotDis(Math::Random::generator()),
				rotDis(Math::Random::generator()),
				rotDis(Math::Random::generator())
				});
			glm::vec3 scale{ scaleDis(Math::Random::generator()) };

			auto cubeInstance = registry.create(std::format("Cube {}", i), pos, rot, scale);
			registry.add<Graphics::Mesh>(cubeInstance, cubeMesh);
			registry.add<Graphics::Material>(cubeInstance, cubeMats[i % materialCount]);
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
	void onUpdate(Aegis::Scene::Registry& registry, float deltaSeconds) override
	{
		auto view = registry.view<Aegis::Transform, Rotatable, Aegis::DynamicTag>();
		for (auto [entity, transform, rotatable] : view.each())
		{
			transform.rotation *= glm::angleAxis(rotatable.speed * deltaSeconds, Aegis::Math::World::UP);
		}
	}
};

class DynamicObjects : public Aegis::SceneDescription
{
public:
	/// @brief All objects in a scene are created here
	void initialize(Aegis::Scene::Scene& scene, Aegis::Scripting::ScriptManager& scripts) override
	{
		using namespace Aegis;
		auto& registry = scene.registry();

		// Environment setup
		auto& env = registry.get<Graphics::Environment>(scene.environment());
		env.skybox = Graphics::Texture::loadFromFile(Core::ASSETS_DIR / "Environments/KloppenheimSky.hdr");
		env.irradiance = Graphics::Texture::irradianceMap(env.skybox);
		env.prefiltered = Graphics::Texture::prefilteredMap(env.skybox);

		registry.get<AmbientLight>(scene.ambientLight()).intensity = 0.25f;
		registry.get<DirectionalLight>(scene.directionalLight()).intensity = 2.0f;
		registry.get<Transform>(scene.mainCamera()) = Transform{
			.location = { -200.0f, -200.0f, 150.0f},
			.rotation = glm::radians(glm::vec3{ -32.0f, 0.0f, -45.0f })
		};

		scene.addSystem<RotationSystem>();

		auto cube = Graphics::Loader::load(registry, Core::ASSETS_DIR / "Misc/cube.obj");
		auto& cubeMesh = registry.get<Graphics::Mesh>(cube).staticMesh;
		auto& cubeMat = registry.get<Graphics::Material>(cube).instance;
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
				posDis(Math::Random::generator()),
				posDis(Math::Random::generator()),
				posDis(Math::Random::generator())
			};
			glm::quat rot = glm::radians(glm::vec3{
				rotDis(Math::Random::generator()),
				rotDis(Math::Random::generator()),
				rotDis(Math::Random::generator())
				});
			glm::vec3 scale{ scaleDis(Math::Random::generator()) };

			auto cubeInstance = registry.create(std::format("Cube {}", i), pos, rot, scale);
			registry.add<Graphics::Mesh>(cubeInstance, cubeMesh);
			registry.add<Graphics::Material>(cubeInstance, cubeMat);
			registry.add<Rotatable>(cubeInstance, glm::radians(Math::Random::uniformFloat(10.0f, 90.0f)));
			registry.add<DynamicTag>(cubeInstance); // Mark dynamic, so its updated every frame
		}
	}
};

// 6. Lucy Statue
//     - extremly high poly model (~28 million triangles)

// NOTE: The model is very large and is not included in the repository.
// It is originally available from the Stanford 3D Scanning Repository: https://graphics.stanford.edu/data/3Dscanrep/
class Lucy : public Aegis::SceneDescription
{
public:
	/// @brief All objects in a scene are created here
	void initialize(Aegis::Scene::Scene& scene, Aegis::Scripting::ScriptManager& scripts) override
	{
		using namespace Aegis;
		auto& registry = scene.registry();
		// Environment setup
		auto& env = registry.get<Graphics::Environment>(scene.environment());
		env.skybox = Graphics::Texture::loadFromFile(Core::ASSETS_DIR / "Environments/KloppenheimSky.hdr");
		env.irradiance = Graphics::Texture::irradianceMap(env.skybox);
		env.prefiltered = Graphics::Texture::prefilteredMap(env.skybox);

		registry.get<AmbientLight>(scene.ambientLight()).intensity = 0.25f;
		registry.get<DirectionalLight>(scene.directionalLight()).intensity = 2.0f;

		std::filesystem::path bistroPath = Core::ENGINE_DIR / "temp/Lucy/lucy.gltf";
		AGX_ASSERT_X(std::filesystem::exists(bistroPath),
			"Lucy statue model not found! Please download the model from 'https://graphics.stanford.edu/data/3Dscanrep/' and place it in the 'temp/Lucy' folder.");

		Graphics::Loader::load(registry, bistroPath);
		registry.get<Transform>(scene.mainCamera()) = Transform{
			.location = { 0.0f, 16.0f, 14.0f},
			.rotation = glm::radians(glm::vec3{ -17.0f, 0.0f, 175.0f })
		};
	}
};

auto main() -> int
{
	// Toggle between cpu/gpu driven by changing this constant:
	Aegis::Graphics::Renderer::ENABLE_GPU_DRIVEN_RENDERING;

	Aegis::Engine engine;
	engine.loadScene<Sponza>();
	//engine.loadScene<Bistro>(); // NOTE: Bistro model not included
	//engine.loadScene<HighPolyHighObj>(0);
	//engine.loadScene<HighPolyHighObj>(1);
	//engine.loadScene<LowPolyHighObj>(0);
	//engine.loadScene<LowPolyHighObj>(1);
	//engine.loadScene<DynamicObjects>();
	//engine.loadScene<Lucy>(); // NOTE: Lucy model not included
	engine.run();
}
