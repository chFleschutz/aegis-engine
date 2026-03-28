module;

#include "core/assert.h"
#include "graphics/frame_graph/frame_graph_render_pass.h"
#include "graphics/vulkan/vulkan_tools.h"

export module Aegis.Graphics.RenderPasses.PresentPass;

export namespace Aegis::Graphics
{
	class PresentPass : public FGRenderPass
	{
	public:
		PresentPass(FGResourcePool& pool, SwapChain& swapChain)
			: m_swapChain{ swapChain }
		{
			m_final = pool.addReference("Final",
				FGResource::Usage::TransferSrc);
		}

		virtual auto info() -> FGNode::Info override
		{
			return FGNode::Info{
				.name = "Present",
				.reads = { m_final },
				.writes = {}
			};
		}

		virtual void execute(FGResourcePool& pool, const FrameInfo& frameInfo) override
		{
			VkCommandBuffer cmd = frameInfo.cmd;

			auto& srcTexture = pool.texture(m_final);
			AGX_ASSERT_X(m_swapChain.width() == srcTexture.image().width(), "Swapchain extent does not match source texture extent");
			AGX_ASSERT_X(m_swapChain.height() == srcTexture.image().height(), "Swapchain extent does not match source texture extent");

			VkImage srcImage = srcTexture.image();
			VkImage dstImage = m_swapChain.currentImage();

			Tools::vk::cmdPipelineBarrier(cmd, dstImage, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_ASPECT_COLOR_BIT);

			VkOffset3D extent = { static_cast<int32_t>(m_swapChain.width()), static_cast<int32_t>(m_swapChain.height()), 1 };
			VkImageBlit blitRegion{};
			blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blitRegion.srcSubresource.mipLevel = 0;
			blitRegion.srcSubresource.baseArrayLayer = 0;
			blitRegion.srcSubresource.layerCount = 1;
			blitRegion.srcOffsets[0] = { 0, 0, 0 };
			blitRegion.srcOffsets[1] = extent;
			blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blitRegion.dstSubresource.mipLevel = 0;
			blitRegion.dstSubresource.baseArrayLayer = 0;
			blitRegion.dstSubresource.layerCount = 1;
			blitRegion.dstOffsets[0] = { 0, 0, 0 };
			blitRegion.dstOffsets[1] = extent;

			vkCmdBlitImage(cmd,
				srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &blitRegion, VK_FILTER_LINEAR);

			Tools::vk::cmdPipelineBarrier(cmd, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT);
		}

	private:
		SwapChain& m_swapChain;
		FGResourceHandle m_final;
		FGResourceHandle m_presentImage;
	};
}