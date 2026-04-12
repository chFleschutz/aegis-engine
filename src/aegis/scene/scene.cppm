module;

#include "core/assert.h"

#include <entt/entt.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

export module Aegis.Scene;

export import Aegis.Scene.Registry;
export import Aegis.Scene.Entity;
import Aegis.Math;
import Aegis.Core.Profiler;
import Aegis.Scene.System;

export namespace Aegis::Scene
{
	/// @brief Scene contains all entities and systems
	class Scene
	{
	public:
		Scene() = default;
		Scene(const Scene&) = delete;
		Scene(Scene&&) = delete;
		~Scene() = default;

		auto operator=(const Scene&) -> Scene & = delete;
		auto operator=(Scene&&) -> Scene & = delete;

		[[nodiscard]] auto registry() -> Registry& { return m_registry; }
		[[nodiscard]] auto mainCamera() const -> Entity { return m_mainCamera; }
		[[nodiscard]] auto ambientLight() const -> Entity { return m_ambientLight; }
		[[nodiscard]] auto directionalLight() const -> Entity { return m_directionalLight; }
		[[nodiscard]] auto environment() const -> Entity { return m_skybox; }

		void setMainCamera(Entity camera) { m_mainCamera = camera; }
		void setAmbientLight(Entity light) { m_ambientLight = light; }
		void setDirectionalLight(Entity light) { m_directionalLight = light; }
		void setEnvironment(Entity environment) { m_skybox = environment; }

		template <SystemDerived T, typename... Args>
			requires std::constructible_from<T, Args...>
		void addSystem(Args&&... args)
		{
			m_systems.emplace_back(std::make_unique<T>(std::forward<Args>(args)...));
		}

		void begin()
		{
			for (auto& system : m_systems)
			{
				system->onBegin(m_registry);
			}
		}

		void update(float deltaSeconds)
		{
			Aegis::ScopeProfiler profiler("Scene Update");

			for (auto& system : m_systems)
			{
				system->onUpdate(m_registry, deltaSeconds);
			}
		}

		void reset()
		{
			// TODO: Clear old scene
		}

	private:
		Registry m_registry;
		std::vector<std::unique_ptr<System>> m_systems;

		Entity m_mainCamera;
		Entity m_ambientLight;
		Entity m_directionalLight;
		Entity m_skybox;
	};
}
