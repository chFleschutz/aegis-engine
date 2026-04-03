module;

#include <vector>

export module Aegis.Graphics.RenderPasses.GeometryPass;

import Aegis.Math;
import Aegis.Graphics.Bindless;
import Aegis.Graphics.FrameGraph;
import Aegis.Graphics.FrameGraph.RenderPass;
import Aegis.Graphics.RenderSystem;
import Aegis.Graphics.Descriptors;
import Aegis.Graphics.Frustum;

export namespace Aegis::Graphics
{
	struct GBufferUbo
	{
		glm::mat4 projection{ 1.0f };
		glm::mat4 view{ 1.0f };
		glm::mat4 inverseView{ 1.0f };
		Frustum frustum;
		glm::vec3 cameraPosition;
	};

	class GeometryPass : public FGRenderPass
	{
	public:
		GeometryPass(FGResourcePool& pool)
			: m_globalUbo{ Buffer::uniformBuffer(sizeof(GBufferUbo)) },
			m_globalSetLayout{ createDescriptorSetLayout() }
		{
			m_globalSets.reserve(MAX_FRAMES_IN_FLIGHT);
			for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
			{
				m_globalSets.emplace_back(m_globalSetLayout);
				DescriptorWriter{ m_globalSetLayout }
					.writeBuffer(0, m_globalUbo.buffer(), i)
					.update(m_globalSets[i]);
			}

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
		}

		virtual auto info() -> FGNode::Info override
		{
			return FGNode::Info{
				.name = "Geometry",
				.reads = {},
				.writes = { m_position, m_normal, m_albedo, m_arm, m_emissive, m_depth }
			};
		}

		virtual void execute(FGResourcePool& pool, const FrameInfo& frameInfo) override
		{
			updateUBO(frameInfo);

			auto& position = pool.texture(m_position);
			auto& normal = pool.texture(m_normal);
			auto& albedo = pool.texture(m_albedo);
			auto& arm = pool.texture(m_arm);
			auto& emissive = pool.texture(m_emissive);
			auto& depth = pool.texture(m_depth);

			VkExtent2D extent = albedo.extent2D();

			auto colorAttachments = std::array{
				Tools::renderingAttachmentInfo(position, VK_ATTACHMENT_LOAD_OP_CLEAR, { 0.0f, 0.0f, 0.0f, 0.0f }),
				Tools::renderingAttachmentInfo(normal, VK_ATTACHMENT_LOAD_OP_CLEAR, { 0.0f, 0.0f, 0.0f, 0.0f }),
				Tools::renderingAttachmentInfo(albedo, VK_ATTACHMENT_LOAD_OP_CLEAR, { 0.0f, 0.0f, 0.0f, 0.0f }),
				Tools::renderingAttachmentInfo(arm, VK_ATTACHMENT_LOAD_OP_CLEAR, { 0.0f, 0.0f, 0.0f, 0.0f }),
				Tools::renderingAttachmentInfo(emissive, VK_ATTACHMENT_LOAD_OP_CLEAR, { 0.0f, 0.0f, 0.0f, 0.0f })
			};

			VkRenderingAttachmentInfo depthAttachment = Tools::renderingAttachmentInfo(
				depth, VK_ATTACHMENT_LOAD_OP_CLEAR, { 1.0f, 0 });

			VkRenderingInfo renderInfo{};
			renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
			renderInfo.renderArea.offset = { 0, 0 };
			renderInfo.renderArea.extent = extent;
			renderInfo.layerCount = 1;
			renderInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
			renderInfo.pColorAttachments = colorAttachments.data();
			renderInfo.pDepthAttachment = &depthAttachment;

			VkCommandBuffer cmd = frameInfo.cmd;
			vkCmdBeginRendering(cmd, &renderInfo);
			{
				Tools::vk::cmdViewport(cmd, extent);
				Tools::vk::cmdScissor(cmd, extent);

				RenderContext ctx{
					.scene = frameInfo.scene,
					.ui = frameInfo.ui,
					.frameIndex = frameInfo.frameIndex,
					.cmd = cmd,
					.globalSet = m_globalSets[frameInfo.frameIndex],
					.globalHandle = m_globalUbo.handle(frameInfo.frameIndex)
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
				.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS |
					VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT)
				.build();
		}

		void updateUBO(const FrameInfo& frameInfo)
		{
			Scene::Entity mainCamera = frameInfo.scene.mainCamera();
			if (!mainCamera)
				return;

			// TODO: Don't update this here (move to renderer or similar)
			auto& camera = mainCamera.get<Camera>();
			camera.aspect = frameInfo.aspectRatio;

			GBufferUbo ubo{
				.projection = glm::rowMajor4(camera.projectionMatrix),
				.view = glm::rowMajor4(camera.viewMatrix),
				.inverseView = glm::rowMajor4(camera.inverseViewMatrix),
				.frustum = Frustum::extractFrom(camera.projectionMatrix * camera.viewMatrix),
				.cameraPosition = mainCamera.get<GlobalTransform>().location
			};

			m_globalUbo.buffer().writeToIndex(&ubo, frameInfo.frameIndex);
		}

		FGResourceHandle m_position;
		FGResourceHandle m_normal;
		FGResourceHandle m_albedo;
		FGResourceHandle m_arm;
		FGResourceHandle m_emissive;
		FGResourceHandle m_depth;

		std::vector<std::unique_ptr<RenderSystem>> m_renderSystems;

		BindlessFrameBuffer m_globalUbo;
		DescriptorSetLayout m_globalSetLayout;
		std::vector<DescriptorSet> m_globalSets;
	};
}