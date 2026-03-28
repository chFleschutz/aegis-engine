module;

#include "graphics/material/material_template.h"

#include "scene/scene.h"

export module Aegis.Graphics.DrawBatchRegistry;

export namespace Aegis::Graphics
{
	struct DrawBatch
	{
		uint32_t batchID;
		uint32_t firstInstance;
		uint32_t instanceCount;
		std::shared_ptr<MaterialTemplate> materialTemplate;
	};

	class DrawBatchRegistry
	{
	public:
		static constexpr uint32_t MAX_DRAW_BATCHES = 256;

		DrawBatchRegistry() = default;
		~DrawBatchRegistry() = default;

		[[nodiscard]] auto isValid(uint32_t batchId) const -> bool { return batchId < static_cast<uint32_t>(m_batches.size()); }
		[[nodiscard]] auto batches() const -> const std::vector<DrawBatch>& { return m_batches; }
		[[nodiscard]] auto batch(uint32_t indexindex) const -> const DrawBatch& { return m_batches[indexindex]; }
		[[nodiscard]] auto batchCount() const -> uint32_t { return static_cast<uint32_t>(m_batches.size()); }
		[[nodiscard]] auto instanceCount() const -> uint32_t { return m_totalCount; }
		[[nodiscard]] auto staticInstanceCount() const -> uint32_t { return m_staticCount; }
		[[nodiscard]] auto dynamicInstanceCount() const -> uint32_t { return m_dynamicCount; }

		auto registerDrawBatch(std::shared_ptr<MaterialTemplate> mat) -> const DrawBatch&
		{
			auto it = std::find_if(m_batches.begin(), m_batches.end(),
				[&mat](const DrawBatch& batch) {
					return batch.materialTemplate == mat;
				});
			if (it != m_batches.end())
				return *it;

			uint32_t nextId = static_cast<uint32_t>(m_batches.size());
			m_batches.emplace_back(nextId, m_totalCount, 0, std::move(mat));
			return m_batches.back();
		}

		void addInstance(uint32_t batchId)
		{
			AGX_ASSERT_X(isValid(batchId), "Invalid batch ID");

			m_batches[batchId].instanceCount++;
			updateOffsets(batchId);
		}

		void removeInstance(uint32_t batchId)
		{
			AGX_ASSERT_X(isValid(batchId), "Invalid batch ID");
			AGX_ASSERT_X(m_batches[batchId].instanceCount > 0, "Batch count is already zero");

			m_batches[batchId].instanceCount--;
			updateOffsets(batchId);
		}

		void sceneChanged(Scene::Scene& scene)
		{
			auto& reg = scene.registry();
			reg.on_construct<Material>().connect<&DrawBatchRegistry::onMaterialCreated>(this);
			reg.on_destroy<Material>().connect<&DrawBatchRegistry::onMaterialRemoved>(this);
			reg.on_construct<DynamicTag>().connect<&DrawBatchRegistry::onDynamicTagCreated>(this);
			reg.on_destroy<DynamicTag>().connect<&DrawBatchRegistry::onDynamicTagRemoved>(this);
		}

	private:
		void updateOffsets(uint32_t startBatchId = 0)
		{
			if (!isValid(startBatchId))
				return;

			uint32_t baseOffset = m_batches[startBatchId].firstInstance + m_batches[startBatchId].instanceCount;
			for (uint32_t i = startBatchId + 1; i < static_cast<uint32_t>(m_batches.size()); i++)
			{
				m_batches[i].firstInstance = baseOffset;
				baseOffset += m_batches[i].instanceCount;
			}
			m_totalCount = baseOffset;
		}

		void onMaterialCreated(entt::registry& reg, entt::entity e)
		{
			const auto& material = reg.get<Material>(e);
			const auto& matTemplate = material.instance->materialTemplate();
			registerDrawBatch(matTemplate);
			addInstance(matTemplate->drawBatch());

			if (reg.all_of<DynamicTag>(e))
			{
				m_dynamicCount++;
			}
			else
			{
				m_staticCount++;
			}
		}

		void onMaterialRemoved(entt::registry& reg, entt::entity e)
		{
			const auto& material = reg.get<Material>(e);
			const auto& matTemplate = material.instance->materialTemplate();
			removeInstance(matTemplate->drawBatch());

			if (reg.all_of<DynamicTag>(e))
			{
				m_dynamicCount--;
			}
			else
			{
				m_staticCount--;
			}
		}

		void onDynamicTagCreated(entt::registry& reg, entt::entity e)
		{
			if (!reg.all_of<Material>(e))
				return;

			m_staticCount--;
			m_dynamicCount++;
		}

		void onDynamicTagRemoved(entt::registry& reg, entt::entity e)
		{
			if (!reg.all_of<Material>(e))
				return;

			m_staticCount++;
			m_dynamicCount--;
		}

		std::vector<DrawBatch> m_batches;
		uint32_t m_staticCount{ 0 };
		uint32_t m_dynamicCount{ 0 };
		uint32_t m_totalCount{ 0 };
	};
}