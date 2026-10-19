#include <aegis/core/assert.h>

import std;
import aegis.engine;

namespace
{
std::mt19937 rng{ std::random_device{}() };

auto uniformFloat(float min, float max) -> float
{
    return std::uniform_real_distribution<float>(min, max)(rng);
}
}

// 1. Crytek Sponza
//      - baseline standard small scene
class Sponza : public aegis::SceneDescription
{
public:
    /// @brief All objects in a scene are created here
    void initialize(aegis::scene::Scene& scene, aegis::scripting::ScriptManager& scripts) override
    {
        using namespace aegis;
        auto& registry = scene.registry();

        registry.get<aegis::assets::Environment>(scene.environment()) =
            Engine::loadEnvironment(paths::assets() / "Environments/KloppenheimSky.hdr");

        registry.get<AmbientLight>(scene.ambientLight()).intensity = 0.25f;
        registry.get<DirectionalLight>(scene.directionalLight()).intensity = 2.0f;

        Engine::loadModel(paths::assets() / "Sponza/Sponza.gltf");
        registry.get<Transform>(scene.mainCamera()) = Transform{
            .location = { -9.75f, 1.2f, 5.25f },
            .rotation = glm::radians(glm::vec3{ -12.0f, 0.0f, 263.0f })
        };
    }
};

// 2. Lumberyard Bistro
//    - larger scene with a lot of objects and details

// NOTE: The Bistro scene is very large (~1.2 GB) and is not included in the repository.
// It is originally available from NVIDIA's Orca collection as fbx: https://developer.nvidia.com/orca/amazon-lumberyard-bistro
// A gltf version of the scene can be found here: https://github.com/zeux/niagara_bistro
class Bistro : public aegis::SceneDescription
{
public:
    /// @brief All objects in a scene are created here
    void initialize(aegis::scene::Scene& scene, aegis::scripting::ScriptManager& scripts) override
    {
        using namespace aegis;
        auto& registry = scene.registry();

        // Environment setup
        registry.get<aegis::assets::Environment>(scene.environment()) =
            Engine::loadEnvironment(paths::assets() / "Environments/KloppenheimSky.hdr");

        registry.get<AmbientLight>(scene.ambientLight()).intensity = 0.25f;
        registry.get<DirectionalLight>(scene.directionalLight()).intensity = 2.0f;

        std::filesystem::path bistroPath = paths::assets() / "../../temp/Bistro/bistro.gltf";
        AGX_ASSERT_X(std::filesystem::exists(bistroPath),
            "Bistro scene not found! Please download the scene from 'https://github.com/zeux/niagara_bistro' and place it in the 'temp/Bistro' folder.");

        Engine::loadModel(bistroPath);
        registry.get<Transform>(scene.mainCamera()) = Transform{
            .location = { -24.5f, 2.75f, 5.25f },
            .rotation = glm::radians(glm::vec3{ -1.5f, 0.0f, -90.0f })
        };
    }
};

// 3. Large Instanced Scene (100k of the same fairly large objects, like trees, rocks, etc)
//    - test instancing on larger objects
class HighPolyHighObj : public aegis::SceneDescription
{
public:
    int cameraMode = 0; // 0 = inside instances, 1 = outside instances
    HighPolyHighObj(int camMode) :
        cameraMode(camMode)
    {
    }

    /// @brief All objects in a scene are created here
    void initialize(aegis::scene::Scene& scene, aegis::scripting::ScriptManager& scripts) override
    {
        using namespace aegis;
        auto& registry = scene.registry();

        // Environment setup
        registry.get<aegis::assets::Environment>(scene.environment()) =
            Engine::loadEnvironment(paths::assets() / "Environments/KloppenheimSky.hdr");

        registry.get<AmbientLight>(scene.ambientLight()).intensity = 0.25f;
        registry.get<DirectionalLight>(scene.directionalLight()).intensity = 2.0f;
        if (cameraMode == 0)
        {
            // Inside Instances (only parts visible)
            registry.get<Transform>(scene.mainCamera()) = Transform{
                .location = { 30.0f, -30.0f, 5.0f },
                .rotation = glm::radians(glm::vec3{ -20.0f, 0.0f, 45.0f })
            };
        }
        else
        {
            //Outside Instances (everything visible)
            registry.get<Transform>(scene.mainCamera()) = Transform{
                .location = { 150.0f, -150.0f, 100.0f },
                .rotation = glm::radians(glm::vec3{ -35.0f, 0.0f, 45.0f })
            };
        }

        auto scifiHelmet = Engine::loadModel(paths::assets() / "SciFiHelmet/SciFiHelmet.gltf");
        registry.get<Transform>(scifiHelmet).location = { 2.0f, 0.0f, 2.0f };

        scene::Entity meshEntity = scifiHelmet;
        std::shared_ptr<aegis::assets::StaticMesh> mesh;
        std::shared_ptr<aegis::assets::MaterialInstance> materialInstance;
        while (!registry.has<aegis::assets::Mesh, aegis::assets::Material>(meshEntity))
        {
            meshEntity = registry.get<Children>(meshEntity).last;
            AGX_ASSERT_X(meshEntity, "Failed to find mesh and material in SciFiHelmet scene");
        }
        mesh = registry.get<aegis::assets::Mesh>(meshEntity).staticMesh;
        materialInstance = registry.get<aegis::assets::Material>(meshEntity).instance;
        AGX_ASSERT(mesh && materialInstance);

        constexpr int instanceCount = 10'000;
        constexpr float boxSize = 100.0f;

        auto posDis = std::uniform_real_distribution<float>(-boxSize, boxSize);
        auto rotDis = std::uniform_real_distribution<float>(0.0f, 360.0f);
        auto scaleDis = std::uniform_real_distribution<float>(0.5f, 2.0f);
        for (int i = 0; i < instanceCount; i++)
        {
            auto instance = registry.create("SciFiHelmetInstance");
            registry.add<aegis::assets::Mesh>(instance, mesh);
            registry.add<aegis::assets::Material>(instance, materialInstance);
            auto& transform = registry.get<Transform>(instance);
            transform.location = {
                posDis(rng),
                posDis(rng),
                posDis(rng) / 2.0f
            };
            transform.rotation = glm::radians(glm::vec3{ 90.0f, 0.0f, rotDis(rng) });
            transform.scale = glm::vec3{ scaleDis(rng) };
        }
    }
};

// 4. Extreme Instanced Scene (1 million+ of the same small objects, like grass blades, cubes, etc)
//    - test limits of gpu driven rendering
class LowPolyHighObj : public aegis::SceneDescription
{
public:
    int cameraMode = 0; // 0 = inside instances, 1 = outside instances
    LowPolyHighObj(int camMode) :
        cameraMode(camMode)
    {
    }

    /// @brief All objects in a scene are created here
    void initialize(aegis::scene::Scene& scene, aegis::scripting::ScriptManager& scripts) override
    {
        using namespace aegis;
        auto& registry = scene.registry();

        // Environment setup
        registry.get<aegis::assets::Environment>(scene.environment()) =
            Engine::loadEnvironment(paths::assets() / "Environments/KloppenheimSky.hdr");

        registry.get<AmbientLight>(scene.ambientLight()).intensity = 0.25f;
        registry.get<DirectionalLight>(scene.directionalLight()).intensity = 2.0f;

        if (cameraMode == 0)
        {
            // Inside Instances (only parts visible)
            registry.get<Transform>(scene.mainCamera()) = Transform{
                .location = { 30.0f, -30.0f, 5.0f },
                .rotation = glm::radians(glm::vec3{ -20.0f, 0.0f, 45.0f })
            };
        }
        else
        {
            // Outside Instances (everything visible)
            registry.get<Transform>(scene.mainCamera()) = Transform{
                .location = { -500.0f, -500.0f, 330.0f },
                .rotation = glm::radians(glm::vec3{ -30.0f, 0.0f, -45.0f })
            };
        }

        auto cube = Engine::loadModel(paths::assets() / "Misc/cube.obj");
        auto& cubeMesh = registry.get<aegis::assets::Mesh>(cube).staticMesh;

        constexpr int materialCount = 10;
        auto pbrMatTemplate = Engine::defaults().pbrTemplate;
        std::vector<std::shared_ptr<aegis::assets::MaterialInstance>> cubeMats;
        for (int i = 0; i < materialCount; i++)
        {
            auto matInstance = Engine::createMaterial(pbrMatTemplate);
            glm::vec3 color{
                uniformFloat(0.0f, 1.0f),
                uniformFloat(0.0f, 1.0f),
                uniformFloat(0.0f, 1.0f)
            };
            const float metallic = uniformFloat(0.0f, 1.0f);
            const float roughness = uniformFloat(0.0f, 1.0f);
            [[maybe_unused]] auto set = matInstance->setParameter(assets::pbr::albedo, color)
                                            .and_then([&] { return matInstance->setParameter(assets::pbr::metallic, metallic); })
                                            .and_then([&] { return matInstance->setParameter(assets::pbr::roughness, roughness); });
            AGX_ASSERT_X(set, "Failed to set the cube material's parameters");
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
                posDis(rng),
                posDis(rng),
                posDis(rng)
            };
            glm::quat rot = glm::radians(glm::vec3{
                rotDis(rng),
                rotDis(rng),
                rotDis(rng) });
            glm::vec3 scale{ scaleDis(rng) };

            auto cubeInstance = registry.create(std::format("Cube {}", i), pos, rot, scale);
            registry.add<aegis::assets::Mesh>(cubeInstance, cubeMesh);
            registry.add<aegis::assets::Material>(cubeInstance, cubeMats[i % materialCount]);
        }
    }
};

// 5. Dynamic Scene (many moving objects with dynamic tag)
//    - test upload bottleneck for dynamic objects

struct Rotatable
{
    float speed = 1.0f;
};

class RotationSystem : public aegis::scene::System
{
public:
    void onUpdate(aegis::scene::Registry& registry, float deltaSeconds) override
    {
        auto view = registry.view<aegis::Transform, Rotatable, aegis::DynamicTag>();
        for (auto [entity, transform, rotatable] : view.each())
        {
            transform.rotation *= glm::angleAxis(rotatable.speed * deltaSeconds, aegis::math::world::UP);
        }
    }
};

class DynamicObjects : public aegis::SceneDescription
{
public:
    /// @brief All objects in a scene are created here
    void initialize(aegis::scene::Scene& scene, aegis::scripting::ScriptManager& scripts) override
    {
        using namespace aegis;
        auto& registry = scene.registry();

        // Environment setup
        registry.get<aegis::assets::Environment>(scene.environment()) =
            Engine::loadEnvironment(paths::assets() / "Environments/KloppenheimSky.hdr");

        registry.get<AmbientLight>(scene.ambientLight()).intensity = 0.25f;
        registry.get<DirectionalLight>(scene.directionalLight()).intensity = 2.0f;
        registry.get<Transform>(scene.mainCamera()) = Transform{
            .location = { -200.0f, -200.0f, 150.0f },
            .rotation = glm::radians(glm::vec3{ -32.0f, 0.0f, -45.0f })
        };

        scene.addSystem<RotationSystem>();

        auto cube = Engine::loadModel(paths::assets() / "Misc/cube.obj");
        auto& cubeMesh = registry.get<aegis::assets::Mesh>(cube).staticMesh;
        auto& cubeMat = registry.get<aegis::assets::Material>(cube).instance;
        [[maybe_unused]] auto set = cubeMat->setParameter(assets::pbr::albedo, glm::vec3{ 0.8f, 0.1f, 0.1f })
                                        .and_then([&] { return cubeMat->setParameter(assets::pbr::metallic, 1.0f); })
                                        .and_then([&] { return cubeMat->setParameter(assets::pbr::roughness, 0.5f); });
        AGX_ASSERT_X(set, "Failed to set the cube material's parameters");

        constexpr int cubeCount = 10'000;
        constexpr float areaSize = 200.0f;
        std::uniform_real_distribution<float> posDis(-areaSize / 2.0f, areaSize / 2.0f);
        std::uniform_real_distribution<float> rotDis(0.0f, 360.0f);
        std::uniform_real_distribution<float> scaleDis(0.5f, 2.0f);
        for (int i = 0; i < cubeCount; i++)
        {
            glm::vec3 pos{
                posDis(rng),
                posDis(rng),
                posDis(rng)
            };
            glm::quat rot = glm::radians(glm::vec3{
                rotDis(rng),
                rotDis(rng),
                rotDis(rng) });
            glm::vec3 scale{ scaleDis(rng) };

            auto cubeInstance = registry.create(std::format("Cube {}", i), pos, rot, scale);
            registry.add<aegis::assets::Mesh>(cubeInstance, cubeMesh);
            registry.add<aegis::assets::Material>(cubeInstance, cubeMat);
            registry.add<Rotatable>(cubeInstance, glm::radians(uniformFloat(10.0f, 90.0f)));
            registry.add<DynamicTag>(cubeInstance); // Mark dynamic, so its updated every frame
        }
    }
};

// 6. Lucy Statue
//     - extremly high poly model (~28 million triangles)

// NOTE: The model is very large and is not included in the repository.
// It is originally available from the Stanford 3D Scanning Repository: https://graphics.stanford.edu/data/3Dscanrep/
class Lucy : public aegis::SceneDescription
{
public:
    /// @brief All objects in a scene are created here
    void initialize(aegis::scene::Scene& scene, aegis::scripting::ScriptManager& scripts) override
    {
        using namespace aegis;
        auto& registry = scene.registry();
        // Environment setup
        registry.get<aegis::assets::Environment>(scene.environment()) =
            Engine::loadEnvironment(paths::assets() / "Environments/KloppenheimSky.hdr");

        registry.get<AmbientLight>(scene.ambientLight()).intensity = 0.25f;
        registry.get<DirectionalLight>(scene.directionalLight()).intensity = 2.0f;

        std::filesystem::path bistroPath = paths::assets() / "../../temp/Lucy/lucy.gltf";
        AGX_ASSERT_X(std::filesystem::exists(bistroPath),
            "Lucy statue model not found! Please download the model from 'https://graphics.stanford.edu/data/3Dscanrep/' and place it in the 'temp/Lucy' folder.");

        Engine::loadModel(bistroPath);
        registry.get<Transform>(scene.mainCamera()) = Transform{
            .location = { 0.0f, 16.0f, 14.0f },
            .rotation = glm::radians(glm::vec3{ -17.0f, 0.0f, 175.0f })
        };
    }
};

auto main() -> int
{
    // Toggle between cpu/gpu driven by changing this constant:
    // aegis::Engine::ENABLE_GPU_DRIVEN_RENDERING

    aegis::Engine engine;
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
