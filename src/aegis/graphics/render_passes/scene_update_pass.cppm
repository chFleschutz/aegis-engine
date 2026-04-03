module;

#include "core/assert.h"

#include <vector>

export module Aegis.Graphics.RenderPasses.SceneUpdatePass;

import Aegis.Math;
import Aegis.Graphics.Bindless;
import Aegis.Graphics.FrameGraph.RenderPass;
import Aegis.Graphics.Frustum;

export namespace Aegis::Graphics
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

		SceneUpdatePass(FGResourcePool& pool)
		{
			m_staticInstances = pool.addBuffer("StaticInstanceData",
				FGResource::Usage::TransferDst,
				FGBufferInfo{
					.size = sizeof(InstanceData) * MAX_STATIC_INSTANCES,
					.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
					// TODO: Remove the host visible flag and use staging buffer for upload instead
					.allocFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
								  VMA_ALLOCATION_CREATE_MAPPED_BIT,
				});

			m_dynamicInstances = pool.addBuffer("DynamicInstanceData",
				FGResource::Usage::TransferDst,
				FGBufferInfo{
					.size = sizeof(InstanceData) * MAX_DYNAMIC_INSTANCES,
					.instanceCount = MAX_FRAMES_IN_FLIGHT,
					.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
					.allocFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
								  VMA_ALLOCATION_CREATE_MAPPED_BIT,
				});

			m_drawBatchBuffer = pool.addBuffer("DrawBatches",
				FGResource::Usage::TransferDst,
				FGBufferInfo{
					.size = sizeof(DrawBatchData) * DrawBatchRegistry::MAX_DRAW_BATCHES,
					.instanceCount = MAX_FRAMES_IN_FLIGHT,
					.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
					.allocFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
								  VMA_ALLOCATION_CREATE_MAPPED_BIT,
				});

			m_cameraData = pool.addBuffer("CameraData",
				FGResource::Usage::TransferDst,
				FGBufferInfo{
					.size = sizeof(CameraData),
					.instanceCount = MAX_FRAMES_IN_FLIGHT,
					.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					.allocFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
								  VMA_ALLOCATION_CREATE_MAPPED_BIT,
				});
		}

		auto info() -> FGNode::Info override
		{
			return FGNode::Info{
				.name = "Instance Update",
				.reads = {},
				.writes = { m_staticInstances, m_dynamicInstances, m_drawBatchBuffer, m_cameraData },
			};
		}

		virtual void sceneInitialized(FGResourcePool& resources, Scene::Scene& scene) override
		{
			std::vector<InstanceData> staticInstances;
			staticInstances.reserve(MAX_STATIC_INSTANCES); // TODO: use draw batcher for actual count

			uint32_t instanceID = 0;
			auto view = scene.registry().view<GlobalTransform, Mesh, Material>(entt::exclude<DynamicTag>);
			for (const auto& [entity, transform, mesh, material] : view.each())
			{
				if (instanceID >= MAX_STATIC_INSTANCES)
				{
					ALOG::warn("Instance Update: Reached maximum static instance count of {}", MAX_STATIC_INSTANCES);
					break;
				}

				if (!mesh.staticMesh || !material.instance || !material.instance->materialTemplate())
					continue;

				const auto& matInstance = material.instance;
				const auto& matTemplate = matInstance->materialTemplate();

				// Update instance data (if needed)
				matInstance->updateParameters(0);

				// Shader needs both in row major (better packing)
				glm::mat4 modelMatrix = transform.matrix();
				glm::mat3 normalMatrix = glm::inverse(modelMatrix);
				staticInstances.emplace_back(glm::rowMajor4(modelMatrix),
					normalMatrix[0], mesh.staticMesh->meshDataBuffer().handle(),
					normalMatrix[1], matInstance->buffer().handle(0),  // TODO: Would normal use frame index but data is static so its fine i guess?
					normalMatrix[2], matTemplate->drawBatch());

				instanceID++;
			}

			// Copy instance data to mapped buffer
			auto& staticBuffer = resources.buffer(m_staticInstances);
			staticBuffer.buffer().copy(staticInstances, 0);
		}

		virtual void execute(FGResourcePool& pool, const FrameInfo& frameInfo) override
		{
			updateDynamicInstances(pool, frameInfo);
			updateDrawBatches(pool, frameInfo);
			updateCameraData(pool, frameInfo);
		}

	private:
		void updateDynamicInstances(FGResourcePool& pool, const FrameInfo& frameInfo)
		{
			// TODO: Update static instance data on demand (when scene changes)

			std::vector<InstanceData> dynamicInstances;
			dynamicInstances.reserve(frameInfo.drawBatcher.instanceCount()); // TODO: Differentiate static/dynamic counts

			uint32_t instanceID = 0;
			auto view = frameInfo.scene.registry().view<GlobalTransform, Mesh, Material, DynamicTag>();
			for (const auto& [entity, transform, mesh, material] : view.each())
			{
				if (instanceID >= MAX_DYNAMIC_INSTANCES)
				{
					ALOG::warn("Instance Update: Reached maximum instance count of {}", MAX_DYNAMIC_INSTANCES);
					break;
				}

				if (!mesh.staticMesh || !material.instance || !material.instance->materialTemplate())
					continue;

				const auto& matInstance = material.instance;
				const auto& matTemplate = matInstance->materialTemplate();

				// Update instance data (if needed)
				matInstance->updateParameters(frameInfo.frameIndex);

				// Shader needs both in row major (better packing)
				glm::mat4 modelMatrix = transform.matrix();
				glm::mat3 normalMatrix = glm::inverse(modelMatrix);

				dynamicInstances.emplace_back(glm::rowMajor4(modelMatrix),
					normalMatrix[0], mesh.staticMesh->meshDataBuffer().handle(),
					normalMatrix[1], matInstance->buffer().handle(frameInfo.frameIndex),
					normalMatrix[2], matTemplate->drawBatch());

				instanceID++;
			}

			// Copy instance data to mapped buffer
			auto& instanceBuffer = pool.buffer(m_dynamicInstances);
			instanceBuffer.buffer().copy(dynamicInstances, frameInfo.frameIndex);
		}

		void updateDrawBatches(FGResourcePool& pool, const FrameInfo& frameInfo)
		{
			std::vector<DrawBatchData> drawBatchData;
			drawBatchData.reserve(frameInfo.drawBatcher.batchCount());
			for (const auto& batch : frameInfo.drawBatcher.batches())
			{
				drawBatchData.emplace_back(batch.firstInstance, batch.instanceCount);
			}
			auto& drawBatchBuffer = pool.buffer(m_drawBatchBuffer);
			drawBatchBuffer.buffer().copy(drawBatchData, frameInfo.frameIndex);
		}

		void updateCameraData(FGResourcePool& pool, const FrameInfo& frameInfo)
		{
			auto mainCamera = frameInfo.scene.mainCamera();
			AGX_ASSERT_X(mainCamera, "Scene Update Pass: No main camera set in scene");

			// TODO: Don't update this here (move to renderer or similar)
			auto& camera = mainCamera.get<Camera>();
			camera.aspect = frameInfo.aspectRatio;

			auto& cameraTransform = mainCamera.get<GlobalTransform>();
			glm::mat4 viewProjection = camera.projectionMatrix * camera.viewMatrix;

			auto& cameraBuffer = pool.buffer(m_cameraData);
			auto data = cameraBuffer.buffer().data<CameraData>(frameInfo.frameIndex);
			data->view = glm::rowMajor4(camera.viewMatrix);
			data->projection = glm::rowMajor4(camera.projectionMatrix);
			data->viewProjection = glm::rowMajor4(viewProjection);
			data->frustum = Frustum::extractFrom(viewProjection);
			data->cameraPosition = cameraTransform.location;
		}

		FGResourceHandle m_staticInstances;
		FGResourceHandle m_dynamicInstances;
		FGResourceHandle m_drawBatchBuffer;
		FGResourceHandle m_cameraData;
	};
}