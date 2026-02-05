#include "pch.h"
#include "culling_pass.h"

#include "engine.h"
#include "graphics/vulkan/vulkan_tools.h"

namespace Aegis::Graphics
{
	CullingPass::CullingPass(FGResourcePool& pool, DrawBatchRegistry& batcher)
		: m_drawBatcher{ batcher }
	{
		m_pipeline = Pipeline::ComputeBuilder{}
			// TODO: Maybe add convienience method to add bindless layout
			.addDescriptorSetLayout(Engine::renderer().bindlessDescriptorSet().layout())
			.addPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, sizeof(CullingPushConstants))
			.setShaderStage(SHADER_DIR "gpu-driven/culling.slang.spv")
			.build();

		m_staticInstances = pool.addReference("StaticInstanceData",
			FGResource::Usage::ComputeReadStorage);

		m_dynamicInstances = pool.addReference("DynamicInstanceData",
			FGResource::Usage::ComputeReadStorage);

		m_drawBatchBuffer = pool.addReference("DrawBatches",
			FGResource::Usage::ComputeReadStorage);

		m_visibleIndices = pool.addBuffer("VisibleInstances",
			FGResource::Usage::ComputeWriteStorage,
			FGBufferInfo{
				.size = sizeof(uint32_t) * std::max(m_drawBatcher.instanceCount(), 1u),
			});

		m_indirectDrawCommands = pool.addBuffer("IndirectDrawCommands",
			FGResource::Usage::ComputeWriteStorage,
			FGBufferInfo{
				.size = sizeof(VkDrawMeshTasksIndirectCommandEXT) * std::max(m_drawBatcher.instanceCount(), 1u),
			});

		m_indirectDrawCounts = pool.addBuffer("IndirectDrawCounts",
			FGResource::Usage::ComputeWriteStorage,
			FGBufferInfo{
				.size = sizeof(uint32_t) * std::max(m_drawBatcher.batchCount(), 1u),
				.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT
			});

		m_cameraData = pool.addReference("CameraData",
			FGResource::Usage::ComputeReadUniform);
	}

	auto CullingPass::info() -> FGNode::Info
	{
		return FGNode::Info{
			.name = "Culling",
			.reads = { m_cameraData, m_staticInstances, m_dynamicInstances },
			.writes = { m_visibleIndices, m_indirectDrawCommands, m_indirectDrawCounts },
		};
	}

	void CullingPass::execute(FGResourcePool& pool, const FrameInfo& frameInfo)
	{
		// Clear visible counts buffer
		auto& indirectDrawCounts = pool.buffer(m_indirectDrawCounts);
		vkCmdFillBuffer(frameInfo.cmd, indirectDrawCounts.buffer(), 0, indirectDrawCounts.buffer().bufferSize(), 0);
		
		CullingPushConstants push{
			.cameraData = pool.buffer(m_cameraData).handle(frameInfo.frameIndex),
			.staticInstances = pool.buffer(m_staticInstances).handle(),
			.dynamicInstances = pool.buffer(m_dynamicInstances).handle(frameInfo.frameIndex),
			.drawBatches = pool.buffer(m_drawBatchBuffer).handle(frameInfo.frameIndex),
			.visibilityInstances = pool.buffer(m_visibleIndices).handle(),
			.indirectDrawCommands = pool.buffer(m_indirectDrawCommands).handle(),
			.indirectDrawCounts = pool.buffer(m_indirectDrawCounts).handle(),
			.staticInstanceCount = m_drawBatcher.staticInstanceCount(),
			.dynamicInstanceCount = m_drawBatcher.dynamicInstanceCount(),
		};

		m_pipeline.bind(frameInfo.cmd);
		m_pipeline.bindDescriptorSet(frameInfo.cmd, 0, Engine::renderer().bindlessDescriptorSet());
		m_pipeline.pushConstants(frameInfo.cmd, VK_SHADER_STAGE_COMPUTE_BIT, push);

		Tools::vk::cmdDispatch(frameInfo.cmd, m_drawBatcher.instanceCount(), WORKGROUP_SIZE);
	}
}