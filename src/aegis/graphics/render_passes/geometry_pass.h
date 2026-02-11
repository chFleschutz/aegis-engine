#pragma once

#include "graphics/bindless/bindless_buffer.h"
#include "graphics/frame_graph/frame_graph.h"
#include "graphics/frame_graph/frame_graph_render_pass.h"
#include "graphics/render_systems/render_system.h"
#include "graphics/descriptors.h"
#include "graphics/frustum.h"

namespace Aegis::Graphics
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
		GeometryPass(FGResourcePool& pool);

		virtual auto info() -> FGNode::Info override;
		virtual void execute(FGResourcePool& pool, const FrameInfo& frameInfo) override;

		template<typename T, typename... Args>
			requires std::is_base_of_v<RenderSystem, T>&& std::is_constructible_v<T, Args...>
		auto addRenderSystem(Args&&... args) -> T&
		{
			m_renderSystems.emplace_back(std::make_unique<T>(std::forward<Args>(args)...));
			return static_cast<T&>(*m_renderSystems.back());
		}

	private:
		auto createDescriptorSetLayout() -> DescriptorSetLayout;

		void updateUBO(const FrameInfo& frameInfo);

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