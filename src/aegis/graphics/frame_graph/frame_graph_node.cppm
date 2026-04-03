module;

#include "graphics/vulkan/vulkan_include.h"

#include <vector>
#include <string>
#include <memory>

export module Aegis.Graphics.FrameGraph.Node;

import Aegis.Graphics.FrameGraph.Resource;

export namespace Aegis::Graphics
{
	class FGRenderPass;

	struct FGNode
	{
		struct Info
		{
			std::string name;
			std::vector<FGResourceHandle> reads;
			std::vector<FGResourceHandle> writes;
		};

		Info info;
		std::unique_ptr<FGRenderPass> pass;
		VkPipelineStageFlags srcStage{ 0 };
		VkPipelineStageFlags dstStage{ 0 };
		std::vector<VkBufferMemoryBarrier> bufferBarriers;
		std::vector<VkImageMemoryBarrier> imageBarriers;
		std::vector<FGTextureHandle> accessedTextures;
	};
}
