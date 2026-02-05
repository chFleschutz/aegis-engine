#pragma once

#include "core/window.h"
#include "draw_batch_registry.h"
#include "graphics/bindless/bindless_descriptor_set.h"
#include "graphics/frame_graph/frame_graph.h"
#include "graphics/globals.h"
#include "graphics/gpu_timer.h"
#include "graphics/swap_chain.h"
#include "scene/scene.h"
#include "vulkan/vulkan_context.h"

namespace Aegis::Graphics
{
	class Renderer
	{
	public:
		struct FrameContext
		{
			VkFence inFlightFence;
			VkSemaphore imageAvailable;
			VkCommandBuffer commandBuffer;
		};

		static constexpr bool ENABLE_GPU_DRIVEN_RENDERING{ true };
		static auto useGPUDrivenRendering() -> bool 
		{ 
			return ENABLE_GPU_DRIVEN_RENDERING 
				&& VulkanContext::device().features().meshShaderEXT.meshShader
				&& VulkanContext::device().features().meshShaderEXT.taskShader;
		}

		Renderer(Core::Window& window);
		Renderer(const Renderer&) = delete;
		Renderer(Renderer&&) = delete;
		~Renderer();

		auto operator=(const Renderer&) -> Renderer& = delete;
		auto operator=(Renderer&&) noexcept -> Renderer& = delete;

		[[nodiscard]] auto window() -> Core::Window& { return m_window; }
		[[nodiscard]] auto swapChain() -> SwapChain& { return m_swapChain; }
		[[nodiscard]] auto bindlessDescriptorSet() -> BindlessDescriptorSet& { return m_bindlessDescriptorSet; }
		[[nodiscard]] auto drawBatchRegistry() -> DrawBatchRegistry& { return m_drawBatchRegistry; }
		[[nodiscard]] auto frameGraph() -> FrameGraph& { return m_frameGraph; }
		[[nodiscard]] auto aspectRatio() const -> float { return m_swapChain.aspectRatio(); }
		[[nodiscard]] auto isFrameStarted() const -> bool { return m_isFrameStarted; }
		[[nodiscard]] auto currentCommandBuffer() const -> VkCommandBuffer;
		[[nodiscard]] auto frameIndex() const -> uint32_t;

		/// @brief Called when the scene has changed and BEFORE it is initialized
		void sceneChanged(Scene::Scene& scene);

		/// @brief Called when the scene has changed and AFTER it is initialized
		void sceneInitialized(Scene::Scene& scene);

		/// @brief Renders the given scene
		void renderFrame(Scene::Scene& scene, UI::UI& ui);

		/// @brief Waits for the GPU to be idle
		void waitIdle();

	private:
		void createFrameContext();
		void recreateSwapChain();
		void createFrameGraph();

		void beginFrame();
		void endFrame();

		Core::Window& m_window;
		VulkanContext& m_vulkanContext;
		
		SwapChain m_swapChain;
		std::array<FrameContext, MAX_FRAMES_IN_FLIGHT> m_frames;
		uint32_t m_currentFrameIndex{ 0 };
		bool m_isFrameStarted{ false };

		BindlessDescriptorSet m_bindlessDescriptorSet;
		DrawBatchRegistry m_drawBatchRegistry;
		FrameGraph m_frameGraph;

		GPUTimerManager m_gpuTimerManager;
	};
}
