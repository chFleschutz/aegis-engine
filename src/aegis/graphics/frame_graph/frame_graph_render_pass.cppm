module;

#include "graphics/frame_graph/frame_graph_node.h"
#include "graphics/frame_graph/frame_graph_resource_pool.h"
#include "graphics/frame_info.h"

// Included here to avoid issues in child classes
#include "scene/entity.h" 

export module Aegis.Graphics.FrameGraph.RenderPass;

export namespace Aegis::Graphics
{
	class FGRenderPass
	{
	public:
		FGRenderPass() = default;
		FGRenderPass(const FGRenderPass&) = delete;
		FGRenderPass(FGRenderPass&&) = delete;
		virtual ~FGRenderPass() = default;

		auto operator=(const FGRenderPass&) -> FGRenderPass & = delete;
		auto operator=(FGRenderPass&&) -> FGRenderPass & = delete;

		/// @brief Information required to create a FrameGraphNode (primarily for defining inputs and outputs)
		virtual auto info() -> FGNode::Info = 0;

		/// @brief Called when resource should be created (at startup, window resize)
		virtual void createResources(FGResourcePool& resources) {}

		/// @brief Called when the scene has been initialized (after loading)
		virtual void sceneInitialized(FGResourcePool& resources, Scene::Scene& scene) {}

		/// @brief Execute the render pass
		virtual void execute(FGResourcePool& resources, const FrameInfo& frameInfo) = 0;

		/// @brief Draw the UI for the render pass in the renderer panel
		virtual void drawUI() {}
	};
}