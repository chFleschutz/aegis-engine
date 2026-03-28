module;

#include "graphics/frame_graph/frame_graph_render_pass.h"
#include "graphics/render_systems/render_system.h"
#include "graphics/descriptors.h"

#include <vector>

export module Aegis.Graphics.RenderPasses.TransparentPass;

import Aegis.Math;

export namespace Aegis::Graphics
{
	struct TransparentUbo
	{
		glm::mat4 view{ 1.0f };
		glm::mat4 projection{ 1.0f };
	};

	class TransparentPass : public FGRenderPass
	{
	public:
		TransparentPass(FGResourcePool& pool) :
			m_globalUbo{ Buffer::uniformBuffer(sizeof(TransparentUbo)) },
			m_globalSetLayout{ createDescriptorSetLayout() }
		{
			m_globalSets.reserve(MAX_FRAMES_IN_FLIGHT);
			for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
			{
				m_globalSets.emplace_back(m_globalSetLayout);
				DescriptorWriter{ m_globalSetLayout }
					.writeBuffer(0, m_globalUbo, i)
					.update(m_globalSets[i]);
			}

			m_sceneColor = pool.addReference("SceneColor",
				FGResource::Usage::ColorAttachment);

			m_depth = pool.addReference("Depth",
				FGResource::Usage::DepthStencilAttachment);
		}

		virtual auto info() -> FGNode::Info override
		{
			return FGNode::Info{
				.name = "Transparent",
				.reads = { m_depth },
				.writes = { m_sceneColor }
			};
		}

		virtual void execute(FGResourcePool& pool, const FrameInfo& frameInfo) override
		{
			updateUBO(frameInfo);

			auto& sceneColor = pool.texture(m_sceneColor);
			auto& depth = pool.texture(m_depth);
			auto colorAttachment = Tools::renderingAttachmentInfo(sceneColor, VK_ATTACHMENT_LOAD_OP_LOAD, {});
			auto depthAttachment = Tools::renderingAttachmentInfo(depth, VK_ATTACHMENT_LOAD_OP_LOAD, {});

			VkExtent2D extent = frameInfo.swapChainExtent;
			VkRenderingInfo renderingInfo{};
			renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
			renderingInfo.renderArea = { 0, 0, extent.width, extent.height };
			renderingInfo.layerCount = 1;
			renderingInfo.colorAttachmentCount = 1;
			renderingInfo.pColorAttachments = &colorAttachment;
			renderingInfo.pDepthAttachment = &depthAttachment;

			VkCommandBuffer cmd = frameInfo.cmd;
			vkCmdBeginRendering(cmd, &renderingInfo);
			{
				Tools::vk::cmdViewport(cmd, extent);
				Tools::vk::cmdScissor(cmd, extent);

				RenderContext ctx{
					.scene = frameInfo.scene,
					.ui = frameInfo.ui,
					.frameIndex = frameInfo.frameIndex,
					.cmd = cmd,
					.globalSet = m_globalSets[frameInfo.frameIndex]
				};

				for (const auto& system : m_renderSystems)
				{
					system->render(ctx);
				}
			}
			vkCmdEndRendering(cmd);
		}

		template<typename T, typename... Args>
			requires std::is_base_of_v<RenderSystem, T>&& std::is_constructible_v<T, Args...>
		auto addRenderSystem(Args&&... args) -> T&
		{
			m_renderSystems.emplace_back(std::make_unique<T>(std::forward<Args>(args)...));
			return static_cast<T&>(*m_renderSystems.back());
		}

	private:
		auto createDescriptorSetLayout() -> DescriptorSetLayout
		{
			return DescriptorSetLayout::Builder{}
				.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT)
				.build();
		}

		void updateUBO(const FrameInfo& frameInfo)
		{
			Scene::Entity mainCamera = frameInfo.scene.mainCamera();
			if (!mainCamera)
				return;

			// TODO: Check if these need to be transposed for shaders (row-major layout)
			auto& camera = mainCamera.get<Camera>();
			TransparentUbo ubo{
				.view = glm::rowMajor4(camera.viewMatrix),
				.projection = glm::rowMajor4(camera.projectionMatrix)
			};

			m_globalUbo.writeToIndex(&ubo, frameInfo.frameIndex);
		}

		FGResourceHandle m_sceneColor;
		FGResourceHandle m_depth;

		std::vector<std::unique_ptr<RenderSystem>> m_renderSystems;

		Buffer m_globalUbo;
		DescriptorSetLayout m_globalSetLayout;
		std::vector<DescriptorSet> m_globalSets;
	};
}