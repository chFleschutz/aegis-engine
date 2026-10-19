export module aegis.defaults;

import std;
import aegis.scene;
import aegis.scene.systems.camera_system;
import aegis.scene.systems.transform_system;
import aegis.scripting.script_manager;
import aegis.scripting.movement.kinematic_movement_controller;
import aegis.assets;

export namespace aegis
{
/// @brief A camera (with movement controls), ambient and directional light, and 'environment' as the sky.
void createDefaultScene(scene::Scene& scene, scripting::ScriptManager& scriptManager, aegis::assets::Environment environment)
{
    // The camera reads the global transforms, so transforms go first.
    scene.addSystem<scene::TransformSystem>();
    scene.addSystem<scene::CameraSystem>();

    auto& registry = scene.registry();

    auto mainCamera = registry.create("Main Camera");
    registry.add<Camera>(mainCamera);
    registry.add<DynamicTag>(mainCamera);
    registry.get<Transform>(mainCamera) = Transform{
        .location = { 0.0f, -15.0f, 10.0f },
        .rotation = glm::radians(glm::vec3{ -30.0f, 0.0f, 0.0f })
    };
    scene.setMainCamera(mainCamera);
    scriptManager.addScript<scripting::KinematicMovementController>(mainCamera);

    auto ambientLight = registry.create("Ambient Light");
    registry.add<AmbientLight>(ambientLight);
    scene.setAmbientLight(ambientLight);

    auto directionalLight = registry.create("Directional Light");
    registry.add<DirectionalLight>(directionalLight);
    registry.get<Transform>(directionalLight).rotation = glm::radians(glm::vec3{ 60.0f, 0.0f, 45.0f });
    scene.setDirectionalLight(directionalLight);

    auto skybox = registry.create("Skybox");
    registry.add<aegis::assets::Environment>(skybox, std::move(environment));
    scene.setEnvironment(skybox);
}
}
