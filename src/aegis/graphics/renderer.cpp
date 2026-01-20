#include "pch.h"
#include "renderer.h"

#include "core/profiler.h"
#include "graphics/render_passes/bloom_pass.h"
#include "graphics/render_passes/culling_pass.h"
#include "graphics/render_passes/geometry_pass.h"
#include "graphics/render_passes/gpu_driven_geometry.h"
#include "graphics/render_passes/lighting_pass.h"
#include "graphics/render_passes/post_processing_pass.h"
#include "graphics/render_passes/present_pass.h"
#include "graphics/render_passes/scene_update_pass.h"
#include "graphics/render_passes/sky_box_pass.h"
#include "graphics/render_passes/ssao_pass.h"
#include "graphics/render_passes/transparent_pass.h"
#include "graphics/render_passes/ui_pass.h"
#include "graphics/render_systems/bindless_static_mesh_render_system.h"
#include "graphics/render_systems/point_light_render_system.h"
#include "graphics/vulkan/vulkan_context.h"
#include "scene/scene.h"

#include <fstream>

namespace Aegis::Graphics
{
	void benchmarkFrameTimes(const GPUTimerManager& gpuTimer, const DrawBatchRegistry& drawBatcher)
	{
		// Save timings to disk
		static constexpr size_t WARMUP_FRAMES = 1000;
		static constexpr size_t MEASURED_FRAMES = 1000;

		static std::vector<double> gpuFrameTime(MEASURED_FRAMES);
		static std::vector<double> gpuInstanceUpdate(MEASURED_FRAMES);
		static std::vector<double> gpuCulling(MEASURED_FRAMES);
		static std::vector<double> gpuGeometry(MEASURED_FRAMES);
		static std::vector<double> gpuLighting(MEASURED_FRAMES);

		static std::vector<double> cpuTotalFrameTime(MEASURED_FRAMES);
		static std::vector<double> cpuRenderFrameTime(MEASURED_FRAMES);
		static std::vector<double> cpuInstanceUpdate(MEASURED_FRAMES);
		static std::vector<double> cpuCulling(MEASURED_FRAMES);
		static std::vector<double> cpuGeometry(MEASURED_FRAMES);
		static std::vector<double> cpuLighting(MEASURED_FRAMES);
		static std::vector<double> cpuGPUSync(MEASURED_FRAMES);

		static size_t frameCount = 0;

		if (frameCount >= WARMUP_FRAMES and frameCount < WARMUP_FRAMES + MEASURED_FRAMES)
		{
			// Record timings
			size_t index = frameCount - WARMUP_FRAMES;
			for (const auto& timing : gpuTimer.timings())
			{
				if (timing.name == "GPU Frame Time")               gpuFrameTime[index] = timing.timeMs;
				else if (timing.name == "Instance Update")     gpuInstanceUpdate[index] = timing.timeMs;
				else if (timing.name == "Culling")             gpuCulling[index] = timing.timeMs;
				else if (timing.name == "GPU Driven Geometry") gpuGeometry[index] = timing.timeMs;
				else if (timing.name == "Lighting")            gpuLighting[index] = timing.timeMs;
			}

			auto& profiler = Profiler::instance();
			cpuTotalFrameTime[index] = profiler.lastTime("Frame Time");
			cpuRenderFrameTime[index] = profiler.lastTime("CPU Render Frame");
			cpuInstanceUpdate[index] = profiler.lastTime("Instance Update");
			cpuCulling[index] = profiler.lastTime("Culling");
			cpuGeometry[index] = profiler.lastTime("GPU Driven Geometry");
			cpuLighting[index] = profiler.lastTime("Lighting");
			cpuGPUSync[index] = profiler.lastTime("GPU Sync");
		}
		else if (frameCount == WARMUP_FRAMES + MEASURED_FRAMES)
		{
			// Save to disk
			std::ofstream file("frame_times.csv");
			file << "Total instance count," << drawBatcher.instanceCount() << "\n";
			file << "Static instances," << drawBatcher.staticInstanceCount() << "\n";
			file << "Dynamic instances," << drawBatcher.dynamicInstanceCount() << "\n";
			file << "\n";
			file << "Frame,"
				"GPU Frame Time(ms),"
				"GPU Instance Update(ms),"
				"GPU Culling(ms),"
				"GPU Geometry(ms),"
				"GPU Lighting(ms),"
				"CPU Total Frame Time (ms),"
				"CPU Render Frame (ms),"
				"CPU Instance Update (ms),"
				"CPU Culling (ms),"
				"CPU Geometry (ms),"
				"CPU Lighting (ms),"
				"CPU Wait for GPU (ms)\n";
			for (size_t i = 0; i < MEASURED_FRAMES; ++i)
			{
				file << (i + 1) << ","
					<< gpuFrameTime[i] << ","
					<< gpuInstanceUpdate[i] << ","
					<< gpuCulling[i] << ","
					<< gpuGeometry[i] << ","
					<< gpuLighting[i] << ","
					<< cpuTotalFrameTime[i] << ","
					<< cpuRenderFrameTime[i] << ","
					<< cpuInstanceUpdate[i] << ","
					<< cpuCulling[i] << ","
					<< cpuGeometry[i] << ","
					<< cpuLighting[i] << ","
					<< cpuGPUSync[i] << "\n";
			}

			ALOG::info("Saved GPU frame times to frame_times.csv");
		}
		++frameCount;
	}



	Renderer::Renderer(Core::Window& window) :
		m_window{ window },
		m_vulkanContext{ VulkanContext::initialize(m_window) },
		m_swapChain{ window.extent() }
	{
		createFrameContext();
	}

	Renderer::~Renderer()
	{
		for (const auto& frame : m_frames)
		{
			vkFreeCommandBuffers(VulkanContext::device(), VulkanContext::device().commandPool(), 1, &frame.commandBuffer);
			vkDestroySemaphore(VulkanContext::device(), frame.imageAvailable, nullptr);
			vkDestroyFence(VulkanContext::device(), frame.inFlightFence, nullptr);
		}
	}

	auto Renderer::currentCommandBuffer() const -> VkCommandBuffer
	{
		AGX_ASSERT_X(m_isFrameStarted, "Cannot get command buffer when frame not in progress");
		AGX_ASSERT_X(m_frames[m_currentFrameIndex].commandBuffer != VK_NULL_HANDLE, "Command buffer not initialized");

		return m_frames[m_currentFrameIndex].commandBuffer;
	}

	auto Renderer::frameIndex() const -> uint32_t
	{
		AGX_ASSERT_X(m_isFrameStarted, "Cannot get frame index when frame not in progress");
		return m_currentFrameIndex;
	}

	void Renderer::sceneChanged(Scene::Scene& scene)
	{
		m_drawBatchRegistry.sceneChanged(scene);
	}

	void Renderer::sceneInitialized(Scene::Scene& scene)
	{
		createFrameGraph();
		m_frameGraph.compile();
		m_frameGraph.sceneInitialized(scene);
	}

	void Renderer::renderFrame(Scene::Scene& scene, UI::UI& ui)
	{
		AGX_PROFILE_FUNCTION();
		{
			AGX_PROFILE_SCOPE("CPU Render Frame");
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

				AGX_GPU_PROFILE_SCOPE(frameInfo.cmd, "GPU Frame Time");

				m_frameGraph.execute(frameInfo);
			}
			endFrame();
		}

		// TODO: Remove benchmarking code when not needed
		benchmarkFrameTimes(m_gpuTimerManager, m_drawBatchRegistry);
	}

	void Renderer::waitIdle()
	{
		vkDeviceWaitIdle(VulkanContext::device());
	}

	void Renderer::createFrameContext()
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

	void Renderer::recreateSwapChain()
	{
		VkExtent2D extent = m_window.extent();
		while (extent.width == 0 || extent.height == 0) // minimized
		{
			glfwWaitEvents();
			extent = m_window.extent();
		}

		waitIdle();
		m_swapChain.resize(extent);
		m_frameGraph.swapChainResized(extent.width, extent.height);
		m_window.resetResizedFlag();
	}

	void Renderer::createFrameGraph()
	{
		// CPU and GPU Driven Geometry Passes are mutually exclusive 
		// Note: They each need different shaders and pipelines (check asset_manager.cpp)
		if constexpr (!ENABLE_GPU_DRIVEN_RENDERING)
		{
			// CPU Driven Rendering Passes
			m_frameGraph.add<GeometryPass>()
				.addRenderSystem<BindlessStaticMeshRenderSystem>(MaterialType::Opaque);
		}
		else
		{
			// GPU Driven Rendering Passes 
			m_frameGraph.add<CullingPass>(m_drawBatchRegistry);
			m_frameGraph.add<SceneUpdatePass>();
			m_frameGraph.add<GPUDrivenGeometry>();
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

	void Renderer::beginFrame()
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

	void Renderer::endFrame()
	{
		AGX_ASSERT(m_isFrameStarted && "Cannot call endFrame while frame is not in progress");

		m_isFrameStarted = false;
		FrameContext& frame = m_frames[m_currentFrameIndex];
		VK_CHECK(vkEndCommandBuffer(frame.commandBuffer));

		{
			AGX_PROFILE_SCOPE("GPU Sync");

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
}
