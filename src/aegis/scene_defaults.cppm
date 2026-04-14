export module Aegis.Defaults;

import Aegis.Scene;
import Aegis.Scene.Systems.CameraSystem;
import Aegis.Scene.Systems.TransformSystem;
import Aegis.Graphics.Components;
import Aegis.Core.AssetManager;
//import Aegis.Scripting.KinematicMovementController;

export namespace Aegis
{
	void createDefaultScene(Scene::Scene& scene)
	{
		scene.addSystem<Scene::CameraSystem>();
		scene.addSystem<Scene::TransformSystem>();

		auto& registry = scene.registry();

		auto mainCamera = registry.create("Main Camera");
		registry.add<Camera>(mainCamera);
		//registry.add<Scripting::KinematcMovementController>(mainCamera);
		registry.add<DynamicTag>(mainCamera);
		registry.get<Transform>(mainCamera) = Transform{
			.location = { 0.0f, -15.0f, 10.0f },
			.rotation = glm::radians(glm::vec3{ -30.0f, 0.0f, 0.0f })
		};
		scene.setMainCamera(mainCamera);

		auto ambientLight = registry.create("Ambient Light");
		registry.add<AmbientLight>(ambientLight);
		scene.setAmbientLight(ambientLight);

		auto directionalLight = registry.create("Directional Light");
		registry.add<DirectionalLight>(directionalLight);
		registry.get<Transform>(directionalLight).rotation = glm::radians(glm::vec3{ 60.0f, 0.0f, 45.0f });
		scene.setDirectionalLight(directionalLight);

		auto skybox = registry.create("Skybox");
		auto& env = registry.add<Graphics::Environment>(skybox);
		env.skybox = Core::AssetManager::instance().get<Graphics::Texture>("default/cubemap_black");
		env.irradiance = env.skybox;
		env.prefiltered = env.skybox;
		env.brdfLUT = Graphics::Texture::BRDFLUT();
		scene.setEnvironment(skybox);
	}
}
