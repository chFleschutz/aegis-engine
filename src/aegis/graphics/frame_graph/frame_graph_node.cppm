module;

#include "graphics/vulkan/vulkan_include.h"

#include <vector>
#include <string>
#include <memory>

export module Aegis.Graphics.FrameGraph.Node;

export import Aegis.Graphics.FrameGraph.Resource;
export import Aegis.Graphics.FrameGraph.ResourceHandle;
export import Aegis.Graphics.FrameGraph.RenderPass;

export namespace Aegis::Graphics
{
	struct FGNode
	{
		FGRenderPass::Info info;
		std::unique_ptr<FGRenderPass> pass;
		VkPipelineStageFlags srcStage{ 0 };
		VkPipelineStageFlags dstStage{ 0 };
		std::vector<VkBufferMemoryBarrier> bufferBarriers;
		std::vector<VkImageMemoryBarrier> imageBarriers;
		std::vector<FGTextureHandle> accessedTextures;
	};
}
