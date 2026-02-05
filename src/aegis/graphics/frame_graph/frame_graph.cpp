#include "pch.h"
#include "frame_graph.h"

#include "core/profiler.h"
#include "graphics/frame_graph/frame_graph_render_pass.h"
#include "graphics/vulkan/vulkan_context.h"
#include "graphics/vulkan/vulkan_tools.h"

#include "graphics/gpu_timer.h"

// Hash function for ResourceHandle to be used in unordered_map
namespace std
{
	template<>
	struct hash<Aegis::Graphics::FGResourceHandle>
	{
		auto operator()(const Aegis::Graphics::FGResourceHandle& handle) const noexcept -> size_t
		{
			return std::hash<uint32_t>()(handle.handle);
		}
	};
}

namespace Aegis::Graphics
{
	void FrameGraph::compile()
	{
		m_pool.resolveReferences();

		{
			auto graph = buildDependencyGraph();
			m_nodes = topologicalSort(graph);
		}

		// TODO: Compute resource lifetimes for aliasing

		createResources();
		generateBarriers();

		// Print info
		ALOG::info("FrameGraph compiled with {} passes", m_nodes.size());
		for (size_t i = 0; i < m_nodes.size(); i++)
		{
			auto& node = m_pool.node(m_nodes[i]);
			ALOG::info("  [{}] {}", i, node.info.name);
		}
	}

	void FrameGraph::sceneInitialized(Scene::Scene& scene)
	{
		for (const auto& nodeHandle : m_nodes)
		{
			auto& node = m_pool.node(nodeHandle);
			node.pass->sceneInitialized(m_pool, scene);
		}
	}

	void FrameGraph::execute(const FrameInfo& frameInfo)
	{
		AGX_PROFILE_FUNCTION();

		for (auto nodeHandle : m_nodes)
		{
			auto& node = m_pool.node(nodeHandle);
			AGX_GPU_PROFILE_SCOPE(frameInfo.cmd, node.info.name);
			AGX_PROFILE_SCOPE(node.info.name);

			Tools::vk::cmdBeginDebugUtilsLabel(frameInfo.cmd, node.info.name.c_str());
			{
				placeBarriers(frameInfo.cmd, node);
				node.pass->execute(m_pool, frameInfo);
			}
			Tools::vk::cmdEndDebugUtilsLabel(frameInfo.cmd);
		}
	}

	void FrameGraph::swapChainResized(uint32_t width, uint32_t height)
	{
		m_pool.resizeImages(width, height);

		for (const auto& nodeHandle : m_nodes)
		{
			auto& node = m_pool.node(nodeHandle);
			node.pass->createResources(m_pool);
		}
	}

	auto FrameGraph::buildDependencyGraph() -> DependencyGraph
	{
		// Register producers
		std::unordered_map<FGResourceHandle, FGNodeHandle> producers;
		for (const auto& FGNodeHandle : m_nodes)
		{
			auto& node = m_pool.node(FGNodeHandle);
			for (auto& write : node.info.writes)
			{
				const auto& resource = m_pool.resource(write);
				if (!std::holds_alternative<FGReferenceInfo>(resource.info))
				{
					producers[write] = FGNodeHandle;
				}
			}
		}

		// Build adjacency list
		std::vector<std::vector<FGNodeHandle>> adjacency(m_nodes.size());

		// Link write -> write dependencies
		for (const auto& FGNodeHandle : m_nodes)
		{
			auto& node = m_pool.node(FGNodeHandle);

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
		for (const auto& FGNodeHandle : m_nodes)
		{
			auto& node = m_pool.node(FGNodeHandle);
			for (auto& read : node.info.reads)
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

	auto FrameGraph::topologicalSort(const DependencyGraph& adjacency) -> std::vector<FGNodeHandle>
	{
		// Kahn's algorithm

		std::vector<size_t> inDegree(m_nodes.size(), 0);
		for (const auto& edges : adjacency)
		{
			for (const auto& target : edges)
			{
				inDegree[target.handle]++;
			}
		}

		std::queue<FGNodeHandle> queue;

		// Enqueue all nodes with no dependencies
		for (size_t i = 0; i < inDegree.size(); ++i)
		{
			if (inDegree[i] == 0)
			{
				queue.emplace(FGNodeHandle{ static_cast<uint32_t>(i) });
			}
		}

		std::vector<FGNodeHandle> sortedNodes;
		sortedNodes.reserve(m_nodes.size());

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

	void FrameGraph::createResources()
	{
		m_pool.createResources();
		for (const auto& nodeHandle : m_nodes)
		{
			auto& node = m_pool.node(nodeHandle);
			node.pass->createResources(m_pool);
		}
	}

	void FrameGraph::generateBarriers()
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
		for (auto nodeHandle : m_nodes)
		{
			auto& node = m_pool.node(nodeHandle);
			node.imageBarriers.clear();
			node.bufferBarriers.clear();
			node.srcStage = 0;
			node.dstStage = 0;

			for (auto readHandle : node.info.reads)
			{
				barrierLambda(nodeHandle, node, readHandle);
			}

			for (auto writeHandle : node.info.writes)
			{
				barrierLambda(nodeHandle, node, writeHandle);
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
			auto& node = m_pool.node(usage.producer);
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

	auto FrameGraph::generateBarrier(FGNode& node, FGResourceHandle srcHandle, FGResourceHandle dstHandle, FGResourceHandle actualHandle) -> bool
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

	void FrameGraph::placeBarriers(VkCommandBuffer cmd, const FGNode& node)
	{
		Tools::vk::cmdPipelineBarrier(cmd, node.srcStage, node.dstStage,
			node.bufferBarriers, node.imageBarriers);

		// Images track their layout internally, so update it after the barrier
		AGX_ASSERT_X(node.imageBarriers.size() == node.accessedTextures.size(),
			"Mismatched image barriers and accessed textures count in FGNode");
		for (size_t i = 0; i < node.accessedTextures.size(); i++)
		{
			auto& texture = m_pool.texture(node.accessedTextures[i]);
			auto& barrier = node.imageBarriers[i];

			AGX_ASSERT_X(texture.image().image() == barrier.image, "Mismatched VkImage in barrier tracking");
			AGX_ASSERT_X(texture.image().layout() == barrier.oldLayout, "Image layout does not match barriers expected layout");

			texture.image().setLayout(barrier.newLayout);
		}
	}
}