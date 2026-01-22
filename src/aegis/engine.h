#pragma once

#include "core/asset_manager.h"
#include "core/globals.h"
#include "core/input.h"
#include "core/layer_stack.h"
#include "core/logging.h"
#include "graphics/renderer.h"
#include "scene/description.h"
#include "scene/scene.h"
#include "ui/ui.h"

namespace Aegis
{
	class Engine
	{
	public:
		Engine();
		Engine(const Engine&) = delete;
		Engine(Engine&&) = delete;
		~Engine() = default;

		auto operator=(const Engine&) -> Engine& = delete;
		auto operator=(Engine&&) -> Engine& = delete;

		/// @brief Returns the instance of the engine
		[[nodiscard]] static auto instance() -> Engine&;
		[[nodiscard]] static auto assets() -> Core::AssetManager& { return Engine::instance().m_assets; }
		[[nodiscard]] static auto window() -> Core::Window& { return Engine::instance().m_window; }
		[[nodiscard]] static auto renderer() -> Graphics::Renderer& { return Engine::instance().m_renderer; }
		[[nodiscard]] static auto ui() -> UI::UI& { return Engine::instance().m_ui; }
		[[nodiscard]] static auto scene() -> Scene::Scene& { return Engine::instance().m_scene; }

		void run();

		/// @brief Creates a scene from a description
		/// @tparam T Description of the scene (Derived from Scene::Description)
		template<Scene::DescriptionDerived T, typename... Args>
		void loadScene(Args&&... args)
		{
			m_scene.reset();
			m_renderer.sceneChanged(m_scene);
			T description{ std::forward<Args>(args)... };
			description.initialize(m_scene);
			m_scene.begin();
			m_renderer.sceneInitialized(m_scene);
		}

	private:
		void applyFrameBrake(std::chrono::steady_clock::time_point frameBeginTime);

		inline static Engine* s_instance{ nullptr };

		Logging m_logging{};
		Core::AssetManager m_assets{};
		Core::LayerStack m_layerStack{};
		Core::Window m_window{ Core::DEFAULT_WIDTH,Core::DEFAULT_HEIGHT, "Aegis" };
		Graphics::Renderer m_renderer{ m_window };
		UI::UI m_ui{ m_renderer, m_layerStack};
		Input m_input{ m_window };
		Scene::Scene m_scene;
	};
}
