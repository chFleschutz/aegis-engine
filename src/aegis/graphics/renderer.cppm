module;

#include "core/assert.h"
#include "graphics/vulkan/vulkan_include.h"

#include <aegis-log/log.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <GLFW/glfw3.h>

#include <array>
#include <limits>

export module Aegis.Graphics.Renderer;

import Aegis.Core.Window;
import Aegis.Core.Globals;
import Aegis.Core.Profiler;
import Aegis.Graphics.DrawBatchRegistry;
import Aegis.Graphics.Bindless;
import Aegis.Graphics.FrameGraph;
import Aegis.Graphics.RenderPasses.BloomPass;
import Aegis.Graphics.RenderPasses.CullingPass;
import Aegis.Graphics.RenderPasses.SceneUpdatePass;
import Aegis.Graphics.RenderPasses.GPUDrivenGeometry;
import Aegis.Graphics.RenderPasses.GeometryPass;
import Aegis.Graphics.RenderPasses.SkyBoxPass;
import Aegis.Graphics.RenderPasses.LightingPass;
import Aegis.Graphics.RenderPasses.PresentPass;
import Aegis.Graphics.RenderPasses.UIPass;
import Aegis.Graphics.RenderPasses.PostProcessingPass;
import Aegis.Graphics.RenderPasses.BloomPass;
import Aegis.Graphics.RenderPasses.TransparentPass;
import Aegis.Graphics.RenderSystems.BindlessStaticMeshRenderSystem;
import Aegis.Graphics.RenderSystems.PointLightRenderSystem;
import Aegis.Graphics.Globals;
import Aegis.Graphics.GPUTimer;
import Aegis.Graphics.SwapChain;
import Aegis.Graphics.VulkanContext;
import Aegis.Graphics.FrameInfo;
import Aegis.Graphics.MaterialTemplate;
import Aegis.Scene;
import Aegis.UI;

export namespace Aegis::Graphics
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

		Renderer(Core::Window& window) :
			m_window{ window },
			m_vulkanContext{ VulkanContext::initialize(m_window) },
			m_swapChain{ VkExtent2D{ m_window.width(), m_window.height() } }
		{
			createFrameContext();
			setupUI();

			if (useGPUDrivenRendering())
			{
				ALOG::info("Using GPU Driven Rendering");
			}
			else
			{
				ALOG::info("Using CPU Driven Rendering");
			}
		}

		Renderer(const Renderer&) = delete;
		Renderer(Renderer&&) = delete;
		~Renderer()
		{
			for (const auto& frame : m_frames)
			{
				vkFreeCommandBuffers(VulkanContext::device(), VulkanContext::device().commandPool(), 1, &frame.commandBuffer);
				vkDestroySemaphore(VulkanContext::device(), frame.imageAvailable, nullptr);
				vkDestroyFence(VulkanContext::device(), frame.inFlightFence, nullptr);
			}

			ImGui_ImplVulkan_Shutdown();
			ImGui_ImplGlfw_Shutdown();
			ImGui::DestroyContext();

			VulkanContext::destroy();
		}

		auto operator=(const Renderer&) -> Renderer & = delete;
		auto operator=(Renderer&&) noexcept -> Renderer & = delete;

		[[nodiscard]] auto window() -> Core::Window& { return m_window; }
		[[nodiscard]] auto swapChain() -> SwapChain& { return m_swapChain; }
		[[nodiscard]] auto bindlessDescriptorSet() -> Bindless::BindlessDescriptorSet& { return m_bindlessDescriptorSet; }
		[[nodiscard]] auto drawBatchRegistry() -> DrawBatchRegistry& { return m_drawBatchRegistry; }
		[[nodiscard]] auto frameGraph() -> FrameGraph& { return m_frameGraph; }
		[[nodiscard]] auto aspectRatio() const -> float { return m_swapChain.aspectRatio(); }
		[[nodiscard]] auto isFrameStarted() const -> bool { return m_isFrameStarted; }
		[[nodiscard]] auto currentCommandBuffer() const -> VkCommandBuffer
		{
			AGX_ASSERT_X(m_isFrameStarted, "Cannot get command buffer when frame not in progress");
			AGX_ASSERT_X(m_frames[m_currentFrameIndex].commandBuffer != VK_NULL_HANDLE, "Command buffer not initialized");

			return m_frames[m_currentFrameIndex].commandBuffer;
		}

		[[nodiscard]] auto frameIndex() const -> uint32_t
		{
			AGX_ASSERT_X(m_isFrameStarted, "Cannot get frame index when frame not in progress");
			return m_currentFrameIndex;
		}

		/// @brief Called when the scene has changed and BEFORE it is initialized
		void sceneChanged(Scene::Scene& scene)
		{
			m_drawBatchRegistry.sceneChanged(scene);
		}

		/// @brief Called when the scene has changed and AFTER it is initialized
		void sceneInitialized(Scene::Scene& scene)
		{
			createFrameGraph();
			m_frameGraph.compile();
			m_frameGraph.sceneInitialized(scene);
		}

		/// @brief Renders the given scene
		void renderFrame(Scene::Scene& scene, UI::UI& ui)
		{
			ScopeProfiler renderFrame("Render Frame");
			{
				beginFrame();
				{
					AGX_ASSERT_X(m_isFrameStarted, "Frame not started");

					FrameInfo frameInfo{
						.scene = scene,
						.ui = ui,
						.drawBatcher = m_drawBatchRegistry,
						.cmd = currentCommandBuffer(),
						.frameIndex = m_currentFrameIndex,
						.swapChainExtent = m_swapChain.extent(),
						.aspectRatio = m_swapChain.aspectRatio()
					};

					GPUScopeTimer gpuFrameTimer(frameInfo.cmd, "GPU Frame Time");
					ScopeProfiler cpuFrameProfiler("CPU Frame Time");

					m_frameGraph.execute(frameInfo);
				}
				endFrame();
			}
		}

		/// @brief Waits for the GPU to be idle
		void waitIdle()
		{
			vkDeviceWaitIdle(VulkanContext::device());
		}

	private:
		void createFrameContext()
		{
			VkFenceCreateInfo fenceInfo{
				.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
				.flags = VK_FENCE_CREATE_SIGNALED_BIT,
			};

			VkSemaphoreCreateInfo semaphoreInfo{
				.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
			};

			VkCommandBufferAllocateInfo cmdInfo{
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
				.commandPool = VulkanContext::device().commandPool(),
				.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
				.commandBufferCount = 1,
			};

			for (auto& frame : m_frames)
			{
				VK_CHECK(vkCreateFence(VulkanContext::device(), &fenceInfo, nullptr, &frame.inFlightFence));
				VK_CHECK(vkCreateSemaphore(VulkanContext::device(), &semaphoreInfo, nullptr, &frame.imageAvailable));
				VK_CHECK(vkAllocateCommandBuffers(VulkanContext::device(), &cmdInfo, &frame.commandBuffer));
			}
		}

		void recreateSwapChain()
		{
			VkExtent2D extent{ m_window.width(), m_window.height() };
			while (extent.width == 0 || extent.height == 0) // minimized
			{
				glfwWaitEvents();
				extent = VkExtent2D{ m_window.width(), m_window.height() };
			}

			waitIdle();
			m_swapChain.resize(extent);
			m_frameGraph.swapChainResized(extent.width, extent.height);
			m_window.resetResizedFlag();
		}

		void setupUI()
		{
			ImGui_ImplGlfw_InitForVulkan(m_window.glfwWindow(), true);

			auto& device = VulkanContext::device();
			VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
			ImGui_ImplVulkan_InitInfo initInfo{
				.ApiVersion = Graphics::VulkanDevice::API_VERSION,
				.Instance = device.instance(),
				.PhysicalDevice = device.physicalDevice(),
				.Device = device.device(),
				.QueueFamily = device.findPhysicalQueueFamilies().graphicsFamily.value(),
				.Queue = device.graphicsQueue(),
				.DescriptorPool = Graphics::VulkanContext::descriptorPool(),
				.MinImageCount = Graphics::MAX_FRAMES_IN_FLIGHT,
				.ImageCount = Graphics::MAX_FRAMES_IN_FLIGHT,
				.PipelineInfoMain = {
					.RenderPass = VK_NULL_HANDLE,
					.Subpass = 0,
					.MSAASamples = VK_SAMPLE_COUNT_1_BIT,
					.PipelineRenderingCreateInfo = {
						.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
						.colorAttachmentCount = 1,
						.pColorAttachmentFormats = &colorFormat,
					}
				},
				.UseDynamicRendering = true,
			};

			ImGui_ImplVulkan_Init(&initInfo);
		}

		void createFrameGraph()
		{
			// CPU and GPU Driven Geometry Passes are mutually exclusive
			// Note: They each need different shaders and pipelines (check asset_manager.cpp)
			if (Renderer::useGPUDrivenRendering())
			{
				// GPU Driven Rendering Passes
				m_frameGraph.add<CullingPass>(m_drawBatchRegistry);
				m_frameGraph.add<SceneUpdatePass>();
				m_frameGraph.add<GPUDrivenGeometry>();
			}
			else
			{
				// CPU Driven Rendering Passes
				m_frameGraph.add<GeometryPass>()
					.addRenderSystem<BindlessStaticMeshRenderSystem>(MaterialType::Opaque);
			}

			m_frameGraph.add<SkyBoxPass>();
			m_frameGraph.add<LightingPass>();
			m_frameGraph.add<PresentPass>(m_swapChain);
			m_frameGraph.add<UIPass>();
			m_frameGraph.add<PostProcessingPass>();
			m_frameGraph.add<BloomPass>();

			m_frameGraph.add<TransparentPass>()
				.addRenderSystem<PointLightRenderSystem>();

			// TODO: Rework transparent rendering with GPU driven approach (need to sort transparents first)
			// TODO: Alternatively add transparent tag component to avoid iterating all static meshes
			//transparentPass.addRenderSystem<BindlessStaticMeshRenderSystem>(MaterialType::Transparent);

			// Disabled for now (gpu performance heavy + noticable blotches when to close to geometry)
			// TODO: Optimize or replace with better technique (like HBAO)
			//m_frameGraph.add<SSAOPass>();
		}

		void beginFrame()
		{
			AGX_ASSERT(!m_isFrameStarted && "Cannot call beginFrame while already in progress");

			FrameContext& frame = m_frames[m_currentFrameIndex];
			vkWaitForFences(VulkanContext::device(), 1, &frame.inFlightFence, VK_TRUE, std::numeric_limits<uint64_t>::max());

			VkResult result = m_swapChain.acquireNextImage(frame.imageAvailable);
			if (result == VK_ERROR_OUT_OF_DATE_KHR)
			{
				recreateSwapChain();
				result = m_swapChain.acquireNextImage(frame.imageAvailable);
			}
			AGX_ASSERT_X(result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR, "Failed to aquire swap chain image");

			VkCommandBufferBeginInfo beginInfo{
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
				.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
			};

			VK_CHECK(vkResetCommandBuffer(frame.commandBuffer, 0));
			VK_CHECK(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo));
			AGX_ASSERT_X(frame.commandBuffer != VK_NULL_HANDLE, "Failed to begin command buffer");
			m_isFrameStarted = true;

			m_gpuTimerManager.resolveTimings(frame.commandBuffer, m_currentFrameIndex);
		}

		void endFrame()
		{
			AGX_ASSERT(m_isFrameStarted && "Cannot call endFrame while frame is not in progress");

			m_isFrameStarted = false;
			FrameContext& frame = m_frames[m_currentFrameIndex];
			VK_CHECK(vkEndCommandBuffer(frame.commandBuffer));

			{
				ScopeProfiler gpuSync("GPU Sync");

				// Ensure the previous frame using this image has finished (for frameIndex != imageIndex)
				m_swapChain.waitForImageInFlight(frame.inFlightFence);
			}

			VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
			VkSemaphore signalSemaphores[] = { m_swapChain.presentReadySemaphore() };
			VkSubmitInfo submitInfo{
				.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
				.waitSemaphoreCount = 1,
				.pWaitSemaphores = &frame.imageAvailable,
				.pWaitDstStageMask = waitStages,
				.commandBufferCount = 1,
				.pCommandBuffers = &frame.commandBuffer,
				.signalSemaphoreCount = 1,
				.pSignalSemaphores = signalSemaphores,
			};

			vkResetFences(VulkanContext::device(), 1, &frame.inFlightFence);
			VK_CHECK(vkQueueSubmit(VulkanContext::device().graphicsQueue(), 1, &submitInfo, frame.inFlightFence));

			auto result = m_swapChain.present();
			if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_window.wasResized())
			{
				recreateSwapChain();
			}

			m_currentFrameIndex = (m_currentFrameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
			VulkanContext::flushDeletionQueue(m_currentFrameIndex);
		}

		Core::Window& m_window;
		VulkanContext& m_vulkanContext;

		SwapChain m_swapChain;
		std::array<FrameContext, MAX_FRAMES_IN_FLIGHT> m_frames;
		uint32_t m_currentFrameIndex{ 0 };
		bool m_isFrameStarted{ false };

		Bindless::BindlessDescriptorSet m_bindlessDescriptorSet;
		DrawBatchRegistry m_drawBatchRegistry;
		FrameGraph m_frameGraph;

		GPUTimerManager m_gpuTimerManager;
	};
}
