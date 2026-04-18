module;

#include "core/assert.h"
#include "graphics/vulkan/vulkan_include.h"

#include <aegis-log/log.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

export module Aegis.Graphics.FrameGraph;

import Aegis.Graphics.FrameGraph.RenderPass;
import Aegis.Graphics.FrameGraph.ResourcePool;
import Aegis.Graphics.FrameGraph.ResourceHandle;
import Aegis.Graphics.FrameGraph.Node;
import Aegis.Graphics.FrameInfo;
import Aegis.Graphics.RenderContext;
import Aegis.Graphics.VulkanContext;
import Aegis.Core.Profiler;
import Aegis.Graphics.GPUTimer;
import Aegis.Graphics.Vulkan.Tools;
import Aegis.Scene;

namespace std
{
	template<>
	struct hash<Aegis::Graphics::FGResourceHandle>
	{
		auto operator()(const Aegis::Graphics::FGResourceHandle& handle) const noexcept -> std::size_t
		{
			return std::hash<uint32_t>()(handle.handle);
		}
	};
}

export namespace Aegis::Graphics
{
	/// @brief Manages renderpasses and resources for rendering a frame
	class FrameGraph
	{
	public:
		FrameGraph() = default;
		FrameGraph(const FrameGraph&) = delete;
		FrameGraph(FrameGraph&&) = delete;
		~FrameGraph() = default;

		auto operator=(const FrameGraph&) -> FrameGraph = delete;
		auto operator=(FrameGraph&&) -> FrameGraph = delete;

		[[nodiscard]] auto nodes() -> std::vector<FGNodeHandle>& { return m_nodesSorted; }
		[[nodiscard]] auto resourcePool() -> FGResourcePool& { return m_pool; }

		[[nodiscard]] auto queryNode(FGNodeHandle handle) -> FGNode&
		{
			AGX_ASSERT(handle.isValid());
			AGX_ASSERT(handle.handle < m_nodes.size());
			return m_nodes[handle.handle];
		}

		auto addNode(std::unique_ptr<FGRenderPass> pass) -> FGNodeHandle
		{
			auto nodeInfo = pass->info();
			m_nodes.emplace_back(nodeInfo, std::move(pass));
			return FGNodeHandle{ static_cast<uint32_t>(m_nodes.size() - 1) };
		}

		template<typename T, typename... Args>
			requires std::is_base_of_v<FGRenderPass, T>&& std::constructible_from<T, FGResourcePool&, Args...>
		auto add(Args&&... args) -> T&
		{
			auto handle = addNode(std::make_unique<T>(m_pool, std::forward<Args>(args)...));
			m_nodesSorted.emplace_back(handle);
			return static_cast<T&>(*queryNode(handle).pass);
		}

		/// @brief Compiles the frame graph by sorting the nodes and creating resources
		void compile()
		{
			m_pool.resolveReferences();

			{
				auto graph = buildDependencyGraph();
				m_nodesSorted = topologicalSort(graph);
			}

			// TODO: Compute resource lifetimes for aliasing

			createResources();
			generateBarriers();

			// Print info
			ALOG::info("FrameGraph compiled with {} passes", m_nodesSorted.size());
			for (std::size_t i = 0; i < m_nodesSorted.size(); i++)
			{
				auto& node = queryNode(m_nodesSorted[i]);
				ALOG::info("  [{}] {}", i, node.info.name);
			}
		}

		/// @brief Notifies all render passes that the scene has been initialized
		void sceneInitialized(Scene::Scene& scene)
		{
			for (const auto& nodeHandle : m_nodesSorted)
			{
				auto& node = queryNode(nodeHandle);
				node.pass->sceneInitialized(m_pool, scene);
			}
		}

		/// @brief Executes the frame graph by executing each node in order
		void execute(const FrameInfo& frameInfo)
		{
			ScopeProfiler execute("FrameGraph Execute");

			for (auto nodeHandle : m_nodesSorted)
			{
				auto& node = queryNode(nodeHandle);
				GPUScopeTimer gpuScope(frameInfo.cmd, node.info.name.c_str());
				ScopeProfiler cpuScope(node.info.name.c_str());

				Tools::vk::cmdBeginDebugUtilsLabel(frameInfo.cmd, node.info.name.c_str());
				{
					placeBarriers(frameInfo.cmd, node);
					node.pass->execute(m_pool, frameInfo);
				}
				Tools::vk::cmdEndDebugUtilsLabel(frameInfo.cmd);
			}
		}

		/// @brief Resizes all swapchain relative resources textures
		void swapChainResized(uint32_t width, uint32_t height)
		{
			m_pool.resizeImages(width, height);

			// Update VkImage ref in barriers to new resized images
			for (auto& node : m_nodes)
			{
				AGX_ASSERT_X(node.imageBarriers.size() == node.accessedTextures.size(),
					"Mismatched image barriers and accessed textures count in FGNode");
				for (std::size_t i = 0; i < node.accessedTextures.size(); i++)
				{
					auto& tex = m_pool.texture(node.accessedTextures[i]);
					auto& barrier = node.imageBarriers[i];
					barrier.image = tex.image();
				}
			}

			for (const auto& nodeHandle : m_nodesSorted)
			{
				auto& n = queryNode(nodeHandle);
				n.pass->createResources(m_pool);
			}
		}

	private:
		using DependencyGraph = std::vector<std::vector<FGNodeHandle>>;

		auto buildDependencyGraph() -> DependencyGraph
		{
			// Register producers
			std::unordered_map<FGResourceHandle, FGNodeHandle> producers;
			for (const auto& handle : m_nodesSorted)
			{
				auto& n = queryNode(handle);
				for (auto& write : n.info.writes)
				{
					const auto& resource = m_pool.resource(write);
					if (!std::holds_alternative<FGReferenceInfo>(resource.info))
					{
						producers[write] = handle;
					}
				}
			}

			// Build adjacency list
			std::vector<std::vector<FGNodeHandle>> adjacency(m_nodesSorted.size());

			// Link write -> write dependencies
			for (const auto& FGNodeHandle : m_nodesSorted)
			{
				auto& node = queryNode(FGNodeHandle);

				for (auto& write : node.info.writes)
				{
					const auto& resource = m_pool.resource(write);
					if (!std::holds_alternative<FGReferenceInfo>(resource.info))
						continue;

					const auto& refInfo = std::get<FGReferenceInfo>(resource.info);
					auto producer = producers.find(refInfo.handle);
					if (producer == producers.end())
						continue;

					if (FGNodeHandle != producer->second)
					{
						adjacency[FGNodeHandle.handle].emplace_back(producer->second);
						producers[refInfo.handle] = FGNodeHandle; // Update producer to the latest writer
					}
				}
			}

			// Link write -> read dependencies
			for (const auto& FGNodeHandle : m_nodesSorted)
			{
				auto& n = queryNode(FGNodeHandle);
				for (auto& read : n.info.reads)
				{
					const auto& resource = m_pool.resource(read);
					if (!std::holds_alternative<FGReferenceInfo>(resource.info))
						continue;

					const auto& refInfo = std::get<FGReferenceInfo>(resource.info);
					auto producer = producers.find(refInfo.handle);
					if (producer == producers.end())
						continue;

					if (FGNodeHandle != producer->second)
					{
						adjacency[FGNodeHandle.handle].emplace_back(producer->second);
					}
				}
			}

			return adjacency;
		}

		auto topologicalSort(const DependencyGraph& adjacency) -> std::vector<FGNodeHandle>
		{
			// Kahn's algorithm

			std::vector<std::size_t> inDegree(m_nodesSorted.size(), 0);
			for (const auto& edges : adjacency)
			{
				for (const auto& target : edges)
				{
					inDegree[target.handle]++;
				}
			}

			std::queue<FGNodeHandle> queue;

			// Enqueue all nodes with no dependencies
			for (std::size_t i = 0; i < inDegree.size(); ++i)
			{
				if (inDegree[i] == 0)
				{
					queue.emplace(FGNodeHandle{ static_cast<uint32_t>(i) });
				}
			}

			std::vector<FGNodeHandle> sortedNodes;
			sortedNodes.reserve(m_nodesSorted.size());

			while (!queue.empty())
			{
				auto nodeHandle = queue.front();
				queue.pop();

				sortedNodes.emplace_back(nodeHandle);
				for (const auto& neighbor : adjacency[nodeHandle.handle])
				{
					inDegree[neighbor.handle]--;
					if (inDegree[neighbor.handle] == 0)
					{
						queue.emplace(neighbor);
					}
				}
			}

			AGX_ASSERT_X(sortedNodes.size() == m_nodes.size(), "Cycle detected in FrameGraph!");

			// reverse to get correct order
			std::reverse(sortedNodes.begin(), sortedNodes.end());
			return sortedNodes;
		}

		void createResources()
		{
			m_pool.createResources();
			for (const auto& nodeHandle : m_nodesSorted)
			{
				auto& n = queryNode(nodeHandle);
				n.pass->createResources(m_pool);
			}
		}

		void generateBarriers()
		{
			struct UsageInfo
			{
				FGNodeHandle producer;
				FGResourceHandle firstUse;
				FGResourceHandle lastUse;
			};
			std::unordered_map<FGResourceHandle, UsageInfo> usages;

			auto barrierLambda = [this, &usages](FGNodeHandle nodeHandle, FGNode& node, FGResourceHandle handle)
				{
					auto actualResourceHandle = m_pool.actualHandle(handle);
					auto& [producer, firstUse, lastUse] = usages[actualResourceHandle];
					if (lastUse.isValid())
					{
						generateBarrier(node, lastUse, handle, actualResourceHandle);
					}
					else
					{
						producer = nodeHandle;
						firstUse = handle;
					}
					lastUse = handle;
				};

			// Generate barriers for all resources between their uses
			for (auto nodeHandle : m_nodesSorted)
			{
				auto& n = queryNode(nodeHandle);
				n.imageBarriers.clear();
				n.bufferBarriers.clear();
				n.srcStage = 0;
				n.dstStage = 0;

				for (auto readHandle : n.info.reads)
				{
					barrierLambda(nodeHandle, n, readHandle);
				}

				for (auto writeHandle : n.info.writes)
				{
					barrierLambda(nodeHandle, n, writeHandle);
				}
			}

			// Transition images to the correct layout
			auto cmd = VulkanContext::device().beginSingleTimeCommands();
			for (auto& [resourceHandle, usage] : usages)
			{
				auto& res = m_pool.resource(resourceHandle);
				if (!std::holds_alternative<FGTextureInfo>(res.info))
					continue;

				// Transition image layout between frames (last use frame N -> first use frame N + 1)
				auto& node = queryNode(usage.producer);
				auto interFrameBarrier = generateBarrier(node, usage.lastUse, usage.firstUse, resourceHandle);

				// Transition initial image layout (for correct use on the first frame)
				auto& texInfo = std::get<FGTextureInfo>(res.info);
				auto& texture = m_pool.texture(texInfo.handle);
				VkImageLayout requiredLayout = interFrameBarrier
					? node.imageBarriers.back().oldLayout
					: FGResource::toAccessInfo(m_pool.resource(usage.firstUse).usage).layout;
				texture.image().transitionLayout(cmd, requiredLayout);
			}
			VulkanContext::device().endSingleTimeCommands(cmd);
		}

		auto generateBarrier(FGNode& node, FGResourceHandle srcHandle, FGResourceHandle dstHandle,
			FGResourceHandle actualHandle) -> bool 
		{
			const auto& srcResource = m_pool.resource(srcHandle);
			const auto& dstResource = m_pool.resource(dstHandle);
			const auto& actualResource = m_pool.resource(actualHandle);

			auto srcAccessInfo = FGResource::toAccessInfo(srcResource.usage);
			auto dstAccessInfo = FGResource::toAccessInfo(dstResource.usage);

			// TODO: Avoid redundant barriers (like read -> read)

			node.srcStage |= srcAccessInfo.stage;
			node.dstStage |= dstAccessInfo.stage;

			if (std::holds_alternative<FGBufferInfo>(actualResource.info))
			{
				const auto& bufferInfo = std::get<FGBufferInfo>(actualResource.info);
				auto& buffer = m_pool.buffer(bufferInfo.handle);
				VkBufferMemoryBarrier barrier{
					.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
					.srcAccessMask = srcAccessInfo.access,
					.dstAccessMask = dstAccessInfo.access,
					.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
					.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
					.buffer = buffer.buffer(),
					.offset = 0,
					.size = VK_WHOLE_SIZE,
				};
				node.bufferBarriers.emplace_back(barrier);
			}
			else if (std::holds_alternative<FGTextureInfo>(actualResource.info))
			{
				const auto& textureInfo = std::get<FGTextureInfo>(actualResource.info);
				auto& texture = m_pool.texture(textureInfo.handle);
				VkImageMemoryBarrier barrier{
					.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
					.srcAccessMask = srcAccessInfo.access,
					.dstAccessMask = dstAccessInfo.access,
					.oldLayout = srcAccessInfo.layout,
					.newLayout = dstAccessInfo.layout,
					.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
					.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
					.image = texture.image(),
					.subresourceRange = VkImageSubresourceRange{
						.aspectMask = Tools::aspectFlags(texture.image().format()),
						.baseMipLevel = 0,
						.levelCount = texture.image().mipLevels(),
						.baseArrayLayer = 0,
						.layerCount = texture.image().layerCount(),
					}
				};
				node.imageBarriers.emplace_back(barrier);
				node.accessedTextures.emplace_back(textureInfo.handle);
			}
			return true;
		}

		void placeBarriers(VkCommandBuffer cmd, const FGNode& node)
		{
			Tools::vk::cmdPipelineBarrier(cmd, node.srcStage, node.dstStage,
				node.bufferBarriers, node.imageBarriers);

			// Images track their layout internally, so update it after the barrier
			AGX_ASSERT_X(node.imageBarriers.size() == node.accessedTextures.size(),
				"Mismatched image barriers and accessed textures count in FGNode");
			for (std::size_t i = 0; i < node.accessedTextures.size(); i++)
			{
				auto& texture = m_pool.texture(node.accessedTextures[i]);
				auto& barrier = node.imageBarriers[i];

				AGX_ASSERT_X(texture.image().image() == barrier.image, "Mismatched VkImage in barrier tracking");
				AGX_ASSERT_X(texture.image().layout() == barrier.oldLayout, "Image layout does not match barriers expected layout");

				texture.image().setLayout(barrier.newLayout);
			}
		}

		std::vector<FGNode> m_nodes;
		std::vector<FGNodeHandle> m_nodesSorted; // Sorted in execution order
		FGResourcePool m_pool;
	};
}