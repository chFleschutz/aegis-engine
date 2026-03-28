module;

#include "graphics/descriptors.h"
#include "graphics/frame_graph/frame_graph_render_pass.h"
#include "graphics/pipeline.h"
#include "scene/components.h"

#include <vector>

export module Aegis.Graphics.RenderPasses.SkyBoxPass;

import Aegis.Math;

export namespace Aegis::Graphics
{
	struct SkyBoxUniforms
	{
		glm::mat4 viewProjection;
	};

	class SkyBoxPass : public FGRenderPass
	{
	public:
		SkyBoxPass(FGResourcePool& pool) :
			m_descriptorSetLayout{ createDescriptorSetLayout() }
		{
			m_descriptorSets.reserve(MAX_FRAMES_IN_FLIGHT);
			for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
			{
				m_descriptorSets.emplace_back(m_descriptorSetLayout);
			}

			m_pipeline = Pipeline::GraphicsBuilder{}
				.addDescriptorSetLayout(m_descriptorSetLayout)
				.addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(SkyBoxUniforms))
				.addShaderStages(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, SHADER_DIR "sky_box.slang.spv")
				.addColorAttachment(VK_FORMAT_R16G16B16A16_SFLOAT)
				.setDepthAttachment(VK_FORMAT_D32_SFLOAT)
				.setDepthTest(true, false, VK_COMPARE_OP_LESS_OR_EQUAL)
				.setCullMode(VK_CULL_MODE_FRONT_BIT)
				.setVertexBindingDescriptions({ { 0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX } })
				.setVertexAttributeDescriptions({ { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 } })
				.buildUnique();

			// Create vertex buffer
			{
				std::vector<glm::vec3> vertices{
					{-1.0f, -1.0f, -1.0f},
					{ 1.0f, -1.0f, -1.0f},
					{ 1.0f,  1.0f, -1.0f},
					{-1.0f,  1.0f, -1.0f},
					{-1.0f, -1.0f,  1.0f},
					{ 1.0f, -1.0f,  1.0f},
					{ 1.0f,  1.0f,  1.0f},
					{-1.0f,  1.0f,  1.0f}
				};

				m_vertexBuffer = Buffer{ Buffer::vertexBuffer(sizeof(glm::vec3) * vertices.size()) };
				m_vertexBuffer.upload(vertices);
			}

			// Create index buffer
			{
				std::vector<uint32_t> indices{
					0, 3, 2, 2, 1, 0,
					1, 2, 5, 5, 2, 6,
					4, 5, 6, 6, 7, 4,
					0, 4, 7, 7, 3, 0,
					2, 3, 7, 7, 6, 2,
					5, 4, 0, 0, 1, 5,
				};

				m_indexBuffer = Buffer{ Buffer::indexBuffer(sizeof(uint32_t) * indices.size()) };
				m_indexBuffer.upload(indices);
			}

			m_sceneColor = pool.addReference("SceneColor",
				FGResource::Usage::ColorAttachment);

			m_depth = pool.addReference("Depth",
				FGResource::Usage::DepthStencilAttachment);
		}

		virtual auto info() -> FGNode::Info override
		{
			return FGNode::Info{
				.name = "Sky Box",
				.reads = { m_depth },
				.writes = { m_sceneColor },
			};
		}

		virtual void execute(FGResourcePool& pool, const FrameInfo& frameInfo) override
		{
			auto skyBoxEntity = frameInfo.scene.environment();
			if (!skyBoxEntity || !skyBoxEntity.has<Environment>())
				return;

			auto& environment = skyBoxEntity.get<Environment>();
			if (!environment.skybox)
				return;

			VkCommandBuffer cmd = frameInfo.cmd;

			DescriptorWriter{ m_descriptorSetLayout }
				.writeImage(0, environment.skybox->descriptorImageInfo())
				.update(m_descriptorSets[frameInfo.frameIndex]);

			auto& sceneColorTexture = pool.texture(m_sceneColor);
			auto& depthTexture = pool.texture(m_depth);
			auto colorAttachment = Tools::renderingAttachmentInfo(sceneColorTexture, VK_ATTACHMENT_LOAD_OP_LOAD);
			auto depthAttachment = Tools::renderingAttachmentInfo(depthTexture, VK_ATTACHMENT_LOAD_OP_LOAD);

			VkRenderingInfo renderInfo{};
			renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
			renderInfo.renderArea.offset = { 0, 0 };
			renderInfo.renderArea.extent = frameInfo.swapChainExtent;
			renderInfo.layerCount = 1;
			renderInfo.pColorAttachments = &colorAttachment;
			renderInfo.colorAttachmentCount = 1;
			renderInfo.pDepthAttachment = &depthAttachment;

			vkCmdBeginRendering(cmd, &renderInfo);
			Tools::vk::cmdViewport(cmd, frameInfo.swapChainExtent);
			Tools::vk::cmdScissor(cmd, frameInfo.swapChainExtent);

			auto& camera = frameInfo.scene.mainCamera().get<Camera>();
			glm::mat4 viewRotOnly = glm::mat4{ glm::mat3{ camera.viewMatrix } }; // Strip translation from view matrix
			SkyBoxUniforms uniforms{
				.viewProjection = glm::rowMajor4(camera.projectionMatrix * viewRotOnly),
			};

			m_pipeline->bind(cmd);
			m_pipeline->pushConstants(cmd, VK_SHADER_STAGE_VERTEX_BIT, uniforms);
			m_pipeline->bindDescriptorSet(cmd, 0, m_descriptorSets[frameInfo.frameIndex]);

			VkBuffer vertexBuffers[] = { m_vertexBuffer };
			VkDeviceSize offsets[] = { 0 };
			vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
			vkCmdBindIndexBuffer(cmd, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(cmd, 36, 1, 0, 0, 0);

			vkCmdEndRendering(cmd);
		}

	private:
		auto createDescriptorSetLayout() -> DescriptorSetLayout
		{
			return DescriptorSetLayout::Builder{}
				.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
				.build();
		}

		FGResourceHandle m_sceneColor;
		FGResourceHandle m_depth;

		DescriptorSetLayout m_descriptorSetLayout;
		std::vector<DescriptorSet> m_descriptorSets;
		std::unique_ptr<Pipeline> m_pipeline;

		Buffer m_vertexBuffer;
		Buffer m_indexBuffer;
	};
}
