export module Aegis.Scene.Defaults;

import Aegis.Scene;

export namespace Aegis::Scene
{
	void createDefaultScene(Scene& scene)
	{
		addSystem<CameraSystem>();
		addSystem<TransformSystem>();

		m_mainCamera = createEntity("Main Camera");
		add<Camera>(m_mainCamera);
		add<Scripting::KinematcMovementController>(m_mainCamera);
		add<DynamicTag>(m_mainCamera);
		get<Transform>(m_mainCamera) = Transform{
			.location = { 0.0f, -15.0f, 10.0f },
			.rotation = glm::radians(glm::vec3{ -30.0f, 0.0f, 0.0f })
		};

		m_ambientLight = createEntity("Ambient Light");
		add<AmbientLight>(m_ambientLight);

		m_directionalLight = createEntity("Directional Light");
		add<DirectionalLight>(m_directionalLight);
		get<Transform>(m_directionalLight).rotation = glm::radians(glm::vec3{ 60.0f, 0.0f, 45.0f });

		m_skybox = createEntity("Skybox");
		auto& env = add<Environment>(m_skybox);
		env.skybox = Engine::assets().get<Graphics::Texture>("default/cubemap_black");
		env.irradiance = env.skybox;
		env.prefiltered = env.skybox;
		env.brdfLUT = Graphics::Texture::BRDFLUT();
	}
}
