module;

#include "core/assert.h"
#include "graphics/descriptors.h"
#include "graphics/pipeline.h"
#include "graphics/resources/static_mesh.h"
#include "graphics/resources/texture.h"

#include <variant>
#include <unordered_map>

export module Aegis.Graphics.MaterialTemplate;

import Aegis.Math;

export namespace Aegis::Graphics
{
	enum class MaterialType
	{
		Opaque,
		Transparent
	};

	struct MaterialParameter
	{
		using Value = std::variant<
			int32_t,
			uint32_t,
			float,
			glm::vec2,
			glm::vec3,
			glm::vec4,
			std::shared_ptr<Texture>
		>;

		uint32_t binding = 0;
		size_t offset = 0;
		size_t size = 0;
		Value defaultValue;
	};

	class MaterialTemplate : public Core::Asset
	{
	public:
		MaterialTemplate(Pipeline pipeline)
			: m_pipeline{ std::move(pipeline) }
		{}

		[[nodiscard]] static auto alignTo(size_t size, size_t alignment) -> size_t
		{
			return (size + alignment - 1) & ~(alignment - 1);
		}

		[[nodiscard]] static auto std430Alignment(const MaterialParameter::Value& val) -> size_t
		{
			return std::visit([&](auto&& arg) -> size_t {
				using T = std::decay_t<decltype(arg)>;
				if constexpr (std::is_same_v<T, int32_t>)        return 4;
				else if constexpr (std::is_same_v<T, uint32_t>)  return 4;
				else if constexpr (std::is_same_v<T, float>)     return 4;
				else if constexpr (std::is_same_v<T, glm::vec2>) return 8;
				else if constexpr (std::is_same_v<T, glm::vec3>) return 16;
				else if constexpr (std::is_same_v<T, glm::vec4>) return 16;
				else if constexpr (std::is_same_v<T, std::shared_ptr<Texture>>) return sizeof(DescriptorHandle);
				else
				{
					AGX_ASSERT_X(false, "Unknown material parameter type");
					return 16;
				}
				}, val);
		}

		[[nodiscard]] static auto std430Size(const MaterialParameter::Value& val) -> size_t
		{
			return std::visit([&](auto&& arg) -> size_t {
				using T = std::decay_t<decltype(arg)>;
				if constexpr (std::is_same_v<T, int32_t>)        return 4;
				else if constexpr (std::is_same_v<T, uint32_t>)  return 4;
				else if constexpr (std::is_same_v<T, float>)     return 4;
				else if constexpr (std::is_same_v<T, glm::vec2>) return 8;
				else if constexpr (std::is_same_v<T, glm::vec3>) return 12;
				else if constexpr (std::is_same_v<T, glm::vec4>) return 16;
				else if constexpr (std::is_same_v<T, std::shared_ptr<Texture>>) return sizeof(DescriptorHandle);
				else
				{
					AGX_ASSERT_X(false, "Unknown material parameter type");
					return 16;
				}
				}, val);
		}

		[[nodiscard]] auto pipeline() const -> const Pipeline& { return m_pipeline; }
		[[nodiscard]] auto parameterSize() const -> size_t { return m_parameterSize; }
		[[nodiscard]] auto parameters() const -> const std::unordered_map<std::string, MaterialParameter>& { return m_parameters; }
		[[nodiscard]] auto drawBatch() const -> uint32_t { return m_drawBatchId; }
		[[nodiscard]] auto type() const -> MaterialType { return m_materialType; }

		[[nodiscard]] auto hasParameter(const std::string& name) const -> bool
		{
			return m_parameters.contains(name);
		}

		[[nodiscard]] auto queryDefaultParameter(const std::string& name) const -> MaterialParameter::Value
		{
			auto it = m_parameters.find(name);
			if (it != m_parameters.end())
				return it->second.defaultValue;

			AGX_ASSERT_X(false, "Material parameter not found");
			return {};
		}

		void addParameter(const std::string& name, const MaterialParameter::Value& defaultValue)
		{
			AGX_ASSERT_X(!m_parameters.contains(name), "Material parameter already exists");

			MaterialParameter param{
				.offset = alignTo(m_parameterSize, std430Alignment(defaultValue)),
				.size = std430Size(defaultValue),
				.defaultValue = defaultValue,
			};

			if (std::holds_alternative<std::shared_ptr<Texture>>(defaultValue))
			{
				m_textureCount++;
				param.binding = m_textureCount;


			}

			m_parameterSize = param.offset + param.size;
			m_parameters.emplace(name, std::move(param));
		}

		void setDrawBatchId(uint32_t id) { m_drawBatchId = id; }

		void bind(VkCommandBuffer cmd)
		{
			m_pipeline.bind(cmd);
		}

		void bindBindlessSet(VkCommandBuffer cmd)
		{
			m_pipeline.bindDescriptorSet(cmd, 0, Engine::renderer().bindlessDescriptorSet().descriptorSet());
		}

		void pushConstants(VkCommandBuffer cmd, const void* data, size_t size, uint32_t offset = 0)
		{
			m_pipeline.pushConstants(cmd, VK_SHADER_STAGE_ALL, data, size, offset);
		}

		void draw(VkCommandBuffer cmd, const StaticMesh& mesh)
		{
			if (m_pipeline.hasFlag(Pipeline::Flags::MeshShader))
			{
				mesh.drawMeshlets(cmd);
			}
			else
			{
				mesh.draw(cmd);
			}
		}

		void drawInstanced(VkCommandBuffer cmd, uint32_t instanceCount)
		{
			AGX_ASSERT_X(m_pipeline.hasFlag(Pipeline::Flags::MeshShader), "Instanced draw is currently only supported for mesh shader pipelines");
			vkCmdDrawMeshTasksEXT(cmd, instanceCount, 1, 1);
		}

		void printInfo() const
		{
			ALOG::info("Material Template Info:");
			ALOG::info("  Parameter Size: {} bytes", m_parameterSize);
			ALOG::info("  Parameters:");
			for (const auto& [name, param] : m_parameters)
			{
				ALOG::info("    Name: {}, Binding: {}, Offset: {}, Size: {}",
					name, param.binding, param.offset, param.size);
			}
		}

	private:
		// TODO: Store multiple pipelines for different render passes
		// TODO: Create pipeline on demand based on render pass
		Pipeline m_pipeline;

		std::unordered_map<std::string, MaterialParameter> m_parameters;
		size_t m_parameterSize{ 0 };
		uint32_t m_textureCount{ 0 };
		uint32_t m_drawBatchId{ 0 };
		MaterialType m_materialType{ MaterialType::Opaque };
	};
}
