module;

#include "graphics/frame_graph/frame_graph_render_pass.h"
#include "graphics/vulkan/vulkan_tools.h"

export module Aegis.Graphics.RenderPasses.UIPass;

export namespace Aegis::Graphics
{
	class UIPass : public FGRenderPass
	{
	public:
		UIPass(FGResourcePool& pool)
		{
			m_final = pool.addReference("Final",
				FGResource::Usage::ColorAttachment);
		}

		virtual auto info() -> FGNode::Info override
		{
			return FGNode::Info{
				.name = "UI",
				.reads = {},
				.writes = { m_final }
			};
		}

		virtual void execute(FGResourcePool& pool, const FrameInfo& frameInfo) override
		{
			VkCommandBuffer cmd = frameInfo.cmd;

			auto& texture = pool.texture(m_final);
			auto attachment = Tools::renderingAttachmentInfo(texture, VK_ATTACHMENT_LOAD_OP_LOAD, {});

			VkExtent2D extent = frameInfo.swapChainExtent;
			VkRenderingInfo renderingInfo{
				.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
				.renderArea = { 0, 0, extent.width, extent.height },
				.layerCount = 1,
				.colorAttachmentCount = 1,
				.pColorAttachments = &attachment,
			};

			vkCmdBeginRendering(cmd, &renderingInfo);
			{
				frameInfo.ui.render(cmd);
			}
			vkCmdEndRendering(cmd);
		}

	private:
		FGResourceHandle m_final;
	};
}