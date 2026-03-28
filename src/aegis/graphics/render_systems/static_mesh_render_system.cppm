module;

#include "graphics/render_systems/render_system.h"
#include "graphics/material/material_template.h"

export module Aegis.Graphics.RenderSystems.StaticMeshRenderSystem;

import Aegis.Math;

export namespace Aegis::Graphics
{
	class StaticMeshRenderSystem : public RenderSystem
	{
	public:
		struct PushConstantData
		{
			glm::mat4 modelMatrix{ 1.0f };
			glm::mat4 normalMatrix{ 1.0f };
		};

		StaticMeshRenderSystem(MaterialType type = MaterialType::Opaque)
			: m_type(type)
		{}

		virtual void render(const RenderContext& ctx) override
		{
			// TODO: Sort for transparent materials back to front
			// TODO: Maybe also for opaque materials but front to back (avoid overdraw)

			MaterialTemplate* lastMatTemplate = nullptr;
			MaterialInstance* lastMatInstance = nullptr;

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
				PushConstantData push{
					.modelMatrix = transform.matrix(),
					.normalMatrix = Math::normalMatrix(transform.rotation, transform.scale)
				};
				currentMatTemplate->pushConstants(ctx.cmd, &push, sizeof(push));

				// Draw Mesh
				currentMatTemplate->draw(ctx.cmd, *mesh.staticMesh);
			}
		}

	private:
		MaterialType m_type;
	};
}