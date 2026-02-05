#pragma once

#include "graphics/bindless/descriptor_handle.h"
#include "graphics/frame_graph/frame_graph_render_pass.h"
#include "graphics/frustum.h"

namespace Aegis::Graphics
{
	// TODO: Maybe use SoA for better cache use (instead of packed AoS)
	struct alignas(16) InstanceData
	{
		glm::mat3x4 modelMatrix;
		glm::vec3 normalRow0;
		DescriptorHandle meshHandle;
		glm::vec3 normalRow1;
		DescriptorHandle materialHandle;
		glm::vec3 normalRow2;
		uint32_t drawBatchID;
	};

	struct DrawBatchData
	{
		uint32_t instanceOffset;
		uint32_t instanceCount;
	};

	struct CameraData
	{
		glm::mat4 view;
		glm::mat4 projection;
		glm::mat4 viewProjection;
		Frustum frustum;
		glm::vec3 cameraPosition;
	};

	class SceneUpdatePass : public FGRenderPass
	{
	public:
		static constexpr size_t MAX_STATIC_INSTANCES = 1'000'000;
		static constexpr size_t MAX_DYNAMIC_INSTANCES = 10'000;

		SceneUpdatePass(FGResourcePool& pool);
		auto info() -> FGNode::Info override;
		virtual void sceneInitialized(FGResourcePool& resources, Scene::Scene& scene) override;
		virtual void execute(FGResourcePool& pool, const FrameInfo& frameInfo) override;

	private:
		void updateDynamicInstances(FGResourcePool& pool, const FrameInfo& frameInfo);
		void updateDrawBatches(FGResourcePool& pool, const FrameInfo& frameInfo);
		void updateCameraData(FGResourcePool& pool, const FrameInfo& frameInfo);

		FGResourceHandle m_staticInstances;
		FGResourceHandle m_dynamicInstances;
		FGResourceHandle m_drawBatchBuffer;
		FGResourceHandle m_cameraData;
	};
}