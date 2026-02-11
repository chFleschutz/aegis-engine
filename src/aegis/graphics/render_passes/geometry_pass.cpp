#include "pch.h"
#include "geometry_pass.h"

#include "scene/components.h"
#include "graphics/vulkan/vulkan_context.h"
#include "graphics/vulkan/vulkan_tools.h"

#include <glm/gtx/matrix_major_storage.hpp>

namespace Aegis::Graphics
{
	GeometryPass::GeometryPass(FGResourcePool& pool)
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

	auto GeometryPass::info() -> FGNode::Info
	{
		return FGNode::Info{
			.name = "Geometry",
			.reads = {},
			.writes = { m_position, m_normal, m_albedo, m_arm, m_emissive, m_depth }
		};
	}

	void GeometryPass::execute(FGResourcePool& pool, const FrameInfo& frameInfo)
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

	auto GeometryPass::createDescriptorSetLayout() -> DescriptorSetLayout
	{
		return DescriptorSetLayout::Builder{}
			.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS | 
				VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT)
			.build();
	}

	void GeometryPass::updateUBO(const FrameInfo& frameInfo)
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
}
