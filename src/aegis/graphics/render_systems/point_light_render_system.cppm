module;

#include "core/assert.h"
#include "graphics/vulkan/vulkan_include.h"

#include <memory>

export module Aegis.Graphics.RenderSystems.PointLightRenderSystem;

import Aegis.Math;
import Aegis.Graphics.RenderSystem;
import Aegis.Graphics.Pipeline;
import Aegis.Graphics.Descriptors;
import Aegis.Core.Globals;
import Aegis.Scene.Components;

export namespace Aegis::Graphics
{
	class PointLightRenderSystem : public RenderSystem
	{
	public:
		struct PointLightPushConstants // max 128 bytes
		{
			glm::vec4 position{};
			glm::vec4 color{};
			float radius;
		};

		PointLightRenderSystem()
		{
			auto globalSetLayout = DescriptorSetLayout::Builder{}
				.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS |
					VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT)
				.build();

			m_pipeline = Pipeline::GraphicsBuilder{}
				.addDescriptorSetLayout(globalSetLayout)
				.addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(PointLightPushConstants))
				.addShaderStages(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, Core::SHADER_DIR / "point_light.slang.spv")
				.addColorAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, true)
				.setDepthAttachment(VK_FORMAT_D32_SFLOAT)
				.setDepthTest(true, false)
				.setVertexBindingDescriptions({})   // Clear default vertex binding
				.setVertexAttributeDescriptions({}) // Clear default vertex attributes
				.buildUnique();
		}

		virtual void render(const RenderContext& ctx) override
		{
			constexpr float pointLightScale = 0.002f;

			m_pipeline->bind(ctx.cmd);
			m_pipeline->bindDescriptorSet(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.globalSet);

			auto view = ctx.scene.registry().view<GlobalTransform, PointLight>();
			for (auto&& [entity, transform, light] : view.each())
			{
				PointLightPushConstants push{
					.position = glm::vec4{ transform.location, 1.0 },
					.color = glm::vec4{ light.color, 1.0 },
					.radius = light.intensity * transform.scale.x * pointLightScale
				};
				m_pipeline->pushConstants(ctx.cmd, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, push);

				vkCmdDraw(ctx.cmd, 6, 1, 0, 0);
			}
		}

	private:
		std::unique_ptr<Pipeline> m_pipeline;
	};
}
