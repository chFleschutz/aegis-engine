module;

#include "core/assert.h"

#include <entt/entt.hpp>

#include <filesystem>

export module Aegis.Scene;

import Aegis.Math;
import Aegis.Scene.Components;
import Aegis.Scene.Systems;
import Aegis.Scene.Entity;

namespace Aegis::Scripting
{
	class ScriptBase;
}

export namespace Aegis::Scene
{
	/// @brief Scene contains all entities and systems
	class Scene
	{
		friend class Entity;

	public:
		Scene() = default;
		Scene(const Scene&) = delete;
		Scene(Scene&&) = delete;
		~Scene() = default;

		auto operator=(const Scene&) -> Scene & = delete;
		auto operator=(Scene&&) -> Scene & = delete;

		[[nodiscard]] auto registry() -> entt::registry& { return m_registry; }
		[[nodiscard]] auto mainCamera() const -> Entity { return m_mainCamera; }
		[[nodiscard]] auto ambientLight() const -> Entity { return m_ambientLight; }
		[[nodiscard]] auto directionalLight() const -> Entity { return m_directionalLight; }
		[[nodiscard]] auto environment() const -> Entity { return m_skybox; }

		void setMainCamera(Entity camera) { m_mainCamera = camera; }

		template <SystemDerived T, typename... Args>
			requires std::constructible_from<T, Args...>
		void addSystem(Args&&... args)
		{
			m_systems.emplace_back(std::make_unique<T>(std::forward<Args>(args)...));
		}

		/// @brief Creates an entity with a NameComponent and TransformComponent
		/// @note Scene::Entity can be passed by value
		auto createEntity(const std::string& name = std::string(), const glm::vec3& location = glm::vec3{ 0.0f },
			const glm::quat& rotation = glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f }, const glm::vec3& scale = glm::vec3{ 1.0f }) -> Entity
		{
			Entity entity = { m_registry.create(), this };
			entity.add<Name>(name.empty() ? "Entity" : name);
			entity.add<Transform>(location, rotation, scale);
			entity.add<GlobalTransform>();
			entity.add<Parent>();
			entity.add<Siblings>();
			entity.add<Children>();
			return entity;
		}

		void destroyEntity(Entity entity)
		{
			// TODO: Destroy all scripts attached to the entity

			entity.removeParent();
			entity.removeChildren();

			m_registry.destroy(entity);
		}

		/// @brief Adds tracking for a script component to call its virtual functions
		void addScript(Scripting::ScriptBase* script) { m_scriptManager.addScript(script); }

		void begin()
		{
			for (auto& system : m_systems)
			{
				system->onBegin(*this);
			}
		}

		void update(float deltaSeconds)
		{
			AGX_PROFILE_FUNCTION();

			for (auto& system : m_systems)
			{
				system->onUpdate(deltaSeconds, *this);
			}

			m_scriptManager.update(deltaSeconds);
		}

		void reset()
		{
			// TODO: Clear old scene

			addSystem<CameraSystem>();
			addSystem<TransformSystem>();

			m_mainCamera = createEntity("Main Camera");
			m_mainCamera.add<Camera>();
			m_mainCamera.add<Scripting::KinematcMovementController>();
			m_mainCamera.add<DynamicTag>();
			m_mainCamera.get<Transform>() = Transform{
				.location = { 0.0f, -15.0f, 10.0f },
				.rotation = glm::radians(glm::vec3{ -30.0f, 0.0f, 0.0f })
			};

			m_ambientLight = createEntity("Ambient Light");
			m_ambientLight.add<AmbientLight>();

			m_directionalLight = createEntity("Directional Light");
			m_directionalLight.add<DirectionalLight>();
			m_directionalLight.get<Transform>().rotation = glm::radians(glm::vec3{ 60.0f, 0.0f, 45.0f });

			m_skybox = createEntity("Skybox");
			auto& env = m_skybox.add<Environment>();
			env.skybox = Engine::assets().get<Graphics::Texture>("default/cubemap_black");
			env.irradiance = env.skybox;
			env.prefiltered = env.skybox;
			env.brdfLUT = Graphics::Texture::BRDFLUT();
		}

	private:
		entt::registry m_registry;
		std::vector<std::unique_ptr<System>> m_systems;
		Scripting::ScriptManager m_scriptManager;

		Entity m_mainCamera;
		Entity m_ambientLight;
		Entity m_directionalLight;
		Entity m_skybox;
	};
}
