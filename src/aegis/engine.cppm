module;

#include <glfw/glfw3.h>

export module Aegis.Engine;

import Aegis.Core.Logging;
import Aegis.Core.AssetManager;
import Aegis.Core.LayerStack;
import Aegis.Core.Window;
import Aegis.Graphics.Renderer;
import Aegis.UI.UI;
import Aegis.Math;
import Aegis.Core.EditorLayer;
import Aegis.Graphics.Texture;

export namespace Aegis
{
	class Engine
	{
	public:
		Engine()
		{
			AGX_ASSERT_X(!s_instance, "Only one instance of Engine is allowed");
			s_instance = this;

			loadDefaultAssets();
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

		void loadDefaultAssets()
		{
			using namespace Aegis::Graphics;

			// Default Textures

			m_assets.add("default/texture_black", Texture::solidColor(glm::vec4{ 0.0f }));
			m_assets.add("default/texture_white", Texture::solidColor(glm::vec4{ 1.0f }));
			m_assets.add("default/texture_normal", Texture::solidColor(glm::vec4{ 0.5f, 0.5f, 1.0f, 0.0f }));

			m_assets.add("default/cubemap_black", Texture::solidColorCube(glm::vec4{ 0.0f }));
			m_assets.add("default/cubemap_white", Texture::solidColorCube(glm::vec4{ 1.0f }));

			// Default PBR Material
			{
				auto pipeline = []() {
					Pipeline::GraphicsBuilder builder{};
					builder.addDescriptorSetLayout(Engine::renderer().bindlessDescriptorSet().layout())
						.addPushConstantRange(VK_SHADER_STAGE_ALL, 128)
						.addColorAttachment(VK_FORMAT_R16G16B16A16_SFLOAT)
						.addColorAttachment(VK_FORMAT_R16G16B16A16_SFLOAT)
						.addColorAttachment(VK_FORMAT_R8G8B8A8_UNORM)
						.addColorAttachment(VK_FORMAT_R8G8B8A8_UNORM)
						.addColorAttachment(VK_FORMAT_R8G8B8A8_UNORM)
						.setDepthAttachment(VK_FORMAT_D32_SFLOAT);
					if (Renderer::useGPUDrivenRendering())
					{
						return builder
							.addShaderStages(VK_SHADER_STAGE_TASK_BIT_EXT,
								SHADER_DIR "gpu-driven/task_meshlet_cull.slang.spv")
							.addShaderStages(VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
								SHADER_DIR "gpu-driven/mesh_geometry_indirect.slang.spv")
							.addFlag(Pipeline::Flags::MeshShader)
							.build();
					}
					else
					{
						// Vertex shader
						return builder
							.addShaderStages(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
								SHADER_DIR "cpu-driven/vertex_geometry_bindless.slang.spv")
							.build();

						// Mesh shader
						//return builder	
						//	.addShaderStages(VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
						//		SHADER_DIR "cpu-driven/mesh_geometry_bindless.slang.spv")
						//	.addFlag(Pipeline::Flags::MeshShader)
						//	.build();

						// Mesh shader + task shader culling (Need to adjust StaticMesh::drawMeshlets to use group size of 32)
						//return builder
						//	.addShaderStages(VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
						//		SHADER_DIR "cpu-driven/mesh_geometry_cull.slang.spv")
						//	.addFlag(Pipeline::Flags::MeshShader)
						//	.build();
					}
					}();

				auto pbrMatTemplate = std::make_shared<MaterialTemplate>(std::move(pipeline));
				pbrMatTemplate->addParameter("albedo", glm::vec3{ 1.0f, 1.0f, 1.0f });
				pbrMatTemplate->addParameter("emissive", glm::vec3{ 0.0f, 0.0f, 0.0f });
				pbrMatTemplate->addParameter("metallic", 0.0f);
				pbrMatTemplate->addParameter("roughness", 1.0f);
				pbrMatTemplate->addParameter("ambientOcclusion", 1.0f);
				pbrMatTemplate->addParameter("albedoMap", get<Texture>("default/texture_white"));
				pbrMatTemplate->addParameter("normalMap", get<Texture>("default/texture_normal"));
				pbrMatTemplate->addParameter("metalRoughnessMap", get<Texture>("default/texture_white"));
				pbrMatTemplate->addParameter("ambientOcclusionMap", get<Texture>("default/texture_white"));
				pbrMatTemplate->addParameter("emissiveMap", get<Texture>("default/texture_white"));
				m_assets.add("default/PBR_template", pbrMatTemplate);

				auto defaultPBRMaterial = Graphics::MaterialInstance::create(pbrMatTemplate);
				defaultPBRMaterial->setParameter("albedo", glm::vec3{ 0.8f, 0.8f, 0.9f });
				m_assets.add("default/PBR_instance", defaultPBRMaterial);
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
