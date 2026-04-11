module;

#include <vector>
#include <string>

export module Aegis.Graphics.FrameGraph.RenderPass;

export import Aegis.Graphics.FrameGraph.ResourcePool;
export import Aegis.Graphics.FrameGraph.ResourceHandle;
export import Aegis.Graphics.FrameGraph.Resource;
export import Aegis.Graphics.FrameInfo;
export import Aegis.Scene.Entity;
export import Aegis.Scene;

export namespace Aegis::Graphics
{
	class FGRenderPass
	{
	public:
		struct Info
		{
			std::string name;
			std::vector<FGResourceHandle> reads;
			std::vector<FGResourceHandle> writes;
		};

		FGRenderPass() = default;
		FGRenderPass(const FGRenderPass&) = delete;
		FGRenderPass(FGRenderPass&&) = delete;
		virtual ~FGRenderPass() = default;

		auto operator=(const FGRenderPass&) -> FGRenderPass & = delete;
		auto operator=(FGRenderPass&&) -> FGRenderPass & = delete;

		/// @brief Information required to create a FrameGraphNode (primarily for defining inputs and outputs)
		virtual auto info() -> Info = 0;

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