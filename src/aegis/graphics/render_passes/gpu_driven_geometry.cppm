module;

#include "core/assert.h"
#include "graphics/frame_graph/frame_graph_render_pass.h"

#include <array>

export module Aegis.Graphics.RenderPasses.GPUDrivenGeometry;

export namespace Aegis::Graphics
{
	class GPUDrivenGeometry : public FGRenderPass
	{
	public:
		struct PushConstant
		{
			DescriptorHandle cameraData;
			DescriptorHandle staticInstances;
			DescriptorHandle dynamicInstances;
			DescriptorHandle visibility;
			uint32_t batchFirstID;
			uint32_t batchSize;
			uint32_t staticCount;
			uint32_t dynamicCount;
		};

		GPUDrivenGeometry(FGResourcePool& pool)
		{
			m_position = pool.addImage("Position",
				FGResource::Usage::ColorAttachment,
				FGTextureInfo{
					.format = VK_FORMAT_R16G16B16A16_SFLOAT,
					.resizeMode = FGResizeMode::SwapChainRelative
				});

			m_normal = pool.addImage("Normal",
				FGResource::Usage::ColorAttachment,
				FGTextureInfo{
					.format = VK_FORMAT_R16G16B16A16_SFLOAT,
					.resizeMode = FGResizeMode::SwapChainRelative
				});

			m_albedo = pool.addImage("Albedo",
				FGResource::Usage::ColorAttachment,
				FGTextureInfo{
					.format = VK_FORMAT_R8G8B8A8_UNORM,
					.resizeMode = FGResizeMode::SwapChainRelative
				});

			m_arm = pool.addImage("ARM",
				FGResource::Usage::ColorAttachment,
				FGTextureInfo{
					.format = VK_FORMAT_R8G8B8A8_UNORM,
					.resizeMode = FGResizeMode::SwapChainRelative
				});

			m_emissive = pool.addImage("Emissive",
				FGResource::Usage::ColorAttachment,
				FGTextureInfo{
					.format = VK_FORMAT_R8G8B8A8_UNORM,
					.resizeMode = FGResizeMode::SwapChainRelative
				});

			m_depth = pool.addImage("Depth",
				FGResource::Usage::DepthStencilAttachment,
				FGTextureInfo{
					.format = VK_FORMAT_D32_SFLOAT,
					.resizeMode = FGResizeMode::SwapChainRelative
				});

			m_visibleInstances = pool.addReference("VisibleInstances",
				FGResource::Usage::ComputeReadStorage);

			m_staticInstanceData = pool.addReference("StaticInstanceData",
				FGResource::Usage::ComputeReadStorage);

			m_dynamicInstanceData = pool.addReference("DynamicInstanceData",
				FGResource::Usage::ComputeReadStorage);

			m_drawBatches = pool.addReference("DrawBatches",
				FGResource::Usage::ComputeReadStorage);

			m_indirectDrawCommands = pool.addReference("IndirectDrawCommands",
				FGResource::Usage::IndirectBuffer);

			m_indirectDrawCounts = pool.addReference("IndirectDrawCounts",
				FGResource::Usage::IndirectBuffer);

			m_cameraData = pool.addReference("CameraData",
				FGResource::Usage::ComputeReadUniform);
		}

		virtual auto info() -> FGNode::Info override
		{
			return FGNode::Info{
				.name = "GPU Driven Geometry",
				.reads = { m_staticInstanceData, m_dynamicInstanceData, m_visibleInstances,
					m_indirectDrawCommands, m_indirectDrawCounts, m_cameraData },
				.writes = { m_position, m_normal, m_albedo, m_arm, m_emissive, m_depth }
			};
		}

		virtual void execute(FGResourcePool& pool, const FrameInfo& frameInfo) override
		{
			VkRect2D renderArea{
				.offset = { 0, 0 },
				.extent = frameInfo.swapChainExtent
			};

			auto colorAttachments = std::array{
				Tools::renderingAttachmentInfo(pool.texture(m_position), VK_ATTACHMENT_LOAD_OP_CLEAR),
				Tools::renderingAttachmentInfo(pool.texture(m_normal), VK_ATTACHMENT_LOAD_OP_CLEAR),
				Tools::renderingAttachmentInfo(pool.texture(m_albedo), VK_ATTACHMENT_LOAD_OP_CLEAR),
				Tools::renderingAttachmentInfo(pool.texture(m_arm), VK_ATTACHMENT_LOAD_OP_CLEAR),
				Tools::renderingAttachmentInfo(pool.texture(m_emissive), VK_ATTACHMENT_LOAD_OP_CLEAR)
			};
			auto depthAttachment = Tools::renderingAttachmentInfo(pool.texture(m_depth), VK_ATTACHMENT_LOAD_OP_CLEAR, { 1.0f, 0 });

			VkRenderingInfo renderInfo{
				.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
				.renderArea = renderArea,
				.layerCount = 1,
				.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size()),
				.pColorAttachments = colorAttachments.data(),
				.pDepthAttachment = &depthAttachment,
			};

			vkCmdBeginRendering(frameInfo.cmd, &renderInfo);
			{
				Tools::vk::cmdViewport(frameInfo.cmd, renderArea.extent);
				Tools::vk::cmdScissor(frameInfo.cmd, renderArea.extent);

				auto& cameraData = pool.buffer(m_cameraData);
				auto& staticInstanceData = pool.buffer(m_staticInstanceData);
				auto& dynamicInstanceData = pool.buffer(m_dynamicInstanceData);
				auto& visibleInstances = pool.buffer(m_visibleInstances);
				auto& indirectDrawCommands = pool.buffer(m_indirectDrawCommands);
				auto& indirectDrawCounts = pool.buffer(m_indirectDrawCounts);
				for (const auto& batch : frameInfo.drawBatcher.batches())
				{
					PushConstant pushConstants{
						.cameraData = cameraData.handle(frameInfo.frameIndex),
						.staticInstances = staticInstanceData.handle(),
						.dynamicInstances = dynamicInstanceData.handle(frameInfo.frameIndex),
						.visibility = visibleInstances.handle(),
						.batchFirstID = batch.firstInstance,
						.batchSize = batch.instanceCount,
						.staticCount = frameInfo.drawBatcher.staticInstanceCount(),
						.dynamicCount = frameInfo.drawBatcher.dynamicInstanceCount()
					};
					AGX_ASSERT_X(pushConstants.cameraData.isValid(), "GPU Driven Geometry Pass: Invalid camera data handle in push constants");
					batch.materialTemplate->bind(frameInfo.cmd);
					batch.materialTemplate->bindBindlessSet(frameInfo.cmd);
					batch.materialTemplate->pushConstants(frameInfo.cmd, &pushConstants, sizeof(PushConstant));

					vkCmdDrawMeshTasksIndirectCountEXT(frameInfo.cmd,
						indirectDrawCommands.buffer(),
						sizeof(VkDrawMeshTasksIndirectCommandEXT) * batch.firstInstance,
						indirectDrawCounts.buffer(),
						sizeof(uint32_t) * batch.batchID,
						batch.instanceCount,
						sizeof(VkDrawMeshTasksIndirectCommandEXT)
					);
				}
			}
			vkCmdEndRendering(frameInfo.cmd);
		}

	private:
		FGResourceHandle m_position;
		FGResourceHandle m_normal;
		FGResourceHandle m_albedo;
		FGResourceHandle m_arm;
		FGResourceHandle m_emissive;
		FGResourceHandle m_depth;
		FGResourceHandle m_visibleInstances;
		FGResourceHandle m_staticInstanceData;
		FGResourceHandle m_dynamicInstanceData;
		FGResourceHandle m_drawBatches;
		FGResourceHandle m_indirectDrawCommands;
		FGResourceHandle m_indirectDrawCounts;
		FGResourceHandle m_cameraData;
	};
}