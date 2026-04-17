module;

#include "core/assert.h"

export module Aegis.Graphics.RenderSystems.BindlessStaticMeshRenderSystem;

import Aegis.Math;
import Aegis.Graphics.Bindless;
import Aegis.Graphics.RenderSystem;
import Aegis.Graphics.MaterialTemplate;
import Aegis.Graphics.MaterialInstance;
import Aegis.Graphics.StaticMesh;
import Aegis.Graphics.Components;

export namespace Aegis::Graphics
{
	class BindlessStaticMeshRenderSystem : public RenderSystem
	{
	public:
		struct alignas(16) PushConstantData
		{
			glm::mat3x4 modelMatrix;
			glm::vec3 normalRow0; Bindless::DescriptorHandle globalBuffer;
			glm::vec3 normalRow1; Bindless::DescriptorHandle meshBuffer;
			glm::vec3 normalRow2; Bindless::DescriptorHandle materialBuffer;
		};

		BindlessStaticMeshRenderSystem(MaterialType type = MaterialType::Opaque) :
			m_type(type)
		{
			static_assert(sizeof(PushConstantData) <= 128, "Vulkan only guarantees a minimum of 128 bytes of push constant");
		}

		virtual void render(const RenderContext& ctx) override
		{
			// TODO: Sort for transparent materials back to front
			// TODO: Maybe also for opaque materials but front to back (avoid overdraw)

			MaterialTemplate* lastMatTemplate = nullptr;
			MaterialInstance* lastMatInstance = nullptr;
			uint32_t objectIndex = 0;
			auto view = ctx.scene.registry().view<GlobalTransform, Mesh, Material>();
			view.use<Material>();
			for (const auto& [entity, transform, mesh, material] : view.each())
			{
				if (!mesh.staticMesh || !material.instance)
					continue;

				auto currentMatTemplate = material.instance->materialTemplate().get();
				if (!currentMatTemplate || currentMatTemplate->type() != m_type)
					continue;

				// Bind Pipeline
				if (lastMatTemplate != currentMatTemplate)
				{
					currentMatTemplate->bind(ctx.cmd);
					currentMatTemplate->bindBindlessSet(ctx.cmd);
					lastMatTemplate = currentMatTemplate;
				}

				// Bind Descriptor Set
				if (lastMatInstance != material.instance.get())
				{
					material.instance->updateParameters(ctx.frameIndex);
					lastMatInstance = material.instance.get();
				}

				// Push Constants
				auto globalTransform = transform.matrix();
				auto normalMatrix = glm::inverse(glm::mat3{ globalTransform }); // Transpose missing because of row-major storage
				PushConstantData push{
					.modelMatrix = glm::rowMajor4(globalTransform),
					.normalRow0 = normalMatrix[0],
					.globalBuffer = ctx.globalHandle,
					.normalRow1 = normalMatrix[1],
					.meshBuffer = mesh.staticMesh->meshDataBuffer().handle(),
					.normalRow2 = normalMatrix[2],
					.materialBuffer = material.instance->buffer().handle(ctx.frameIndex)
				};
				AGX_ASSERT_X(push.globalBuffer.isValid(), "Global buffer handle is invalid");
				AGX_ASSERT_X(push.meshBuffer.isValid(), "Mesh buffer handle is invalid");
				AGX_ASSERT_X(push.materialBuffer.isValid(), "Material buffer handle is invalid");

				currentMatTemplate->pushConstants(ctx.cmd, &push, sizeof(push));
				currentMatTemplate->draw(ctx.cmd, *mesh.staticMesh);
			}
		}

	private:
		MaterialType m_type;
	};
}