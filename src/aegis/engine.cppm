module;

#include "core/profiler.h"
#include "core/asset_manager.h"
#include "core/globals.h"
#include "core/input.h"
#include "core/layer_stack.h"
#include "core/logging.h"
#include "graphics/renderer.h"
#include "scene/description.h"
#include "scene/scene.h"
#include "ui/ui.h"

export module Aegis.Engine;

export namespace Aegis
{
	class Engine
	{
	public:
		Engine()
		{
			AGX_ASSERT_X(!s_instance, "Only one instance of Engine is allowed");
			s_instance = this;

			m_assets.loadDefaultAssets();
			m_layerStack.push<Core::EditorLayer>();

			ALOG::info("Engine Initialized!");
			Logging::logo();
		}

		Engine(const Engine&) = delete;
		Engine(Engine&&) = delete;
		~Engine() = default;

		auto operator=(const Engine&) -> Engine & = delete;
		auto operator=(Engine&&) -> Engine & = delete;

		/// @brief Returns the instance of the engine
		[[nodiscard]] static auto instance() -> Engine&
		{
			AGX_ASSERT_X(s_instance, "Engine instance not created yet");
			return *s_instance;
		}

		[[nodiscard]] static auto assets() -> Core::AssetManager& { return Engine::instance().m_assets; }
		[[nodiscard]] static auto window() -> Core::Window& { return Engine::instance().m_window; }
		[[nodiscard]] static auto renderer() -> Graphics::Renderer& { return Engine::instance().m_renderer; }
		[[nodiscard]] static auto ui() -> UI::UI& { return Engine::instance().m_ui; }
		[[nodiscard]] static auto scene() -> Scene::Scene& { return Engine::instance().m_scene; }

		void run()
		{
			// Main Update loop
			auto lastFrameBegin = std::chrono::steady_clock::now();
			while (!m_window.shouldClose())
			{
				AGX_PROFILE_SCOPE("Frame Time");

				// Calculate time
				auto currentFrameBegin = std::chrono::steady_clock::now();
				float frameTimeSec = std::chrono::duration<float, std::chrono::seconds::period>(currentFrameBegin - lastFrameBegin).count();
				lastFrameBegin = currentFrameBegin;

				glfwPollEvents();

				// Update 
				m_scene.update(frameTimeSec);
				m_layerStack.update(frameTimeSec);

				// Rendering
				m_renderer.renderFrame(m_scene, m_ui);

				applyFrameBrake(currentFrameBegin);
			}

			m_renderer.waitIdle();
		}

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
		void applyFrameBrake(std::chrono::steady_clock::time_point frameBegin)
		{
			using namespace std::chrono;

			AGX_PROFILE_SCOPE("Wait for FPS limit");

			if constexpr (!Core::ENABLE_FPS_LIMIT)
				return;

			// Note: This has to be a busy wait since sleep_for is not accurate enough (especially on Windows)
			while (duration<double, std::milli>(steady_clock::now() - frameBegin).count() < Core::TARGET_FRAME_TIME)
			{
				// busy wait
			}
		}

		inline static Engine* s_instance{ nullptr };

		Logging m_logging{};
		Core::AssetManager m_assets{};
		Core::LayerStack m_layerStack{};
		Core::Window m_window{ Core::DEFAULT_WIDTH,Core::DEFAULT_HEIGHT, "Aegis" };
		Graphics::Renderer m_renderer{ m_window };
		UI::UI m_ui{ m_renderer, m_layerStack };
		Input m_input{ m_window };
		Scene::Scene m_scene;
	};
}
