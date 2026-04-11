module;

#include "core/assert.h"
#include "graphics/vulkan/vulkan_include.h"

#include <vector>

export module Aegis.Graphics.StaticMesh;

export import Aegis.Graphics.Vertex;
import Aegis.Math;
import Aegis.Graphics.Vulkan.ResourceTools;
import Aegis.Graphics.Bindless;
import Aegis.Graphics.VulkanContext;
import Aegis.Graphics.Buffer;

export namespace Aegis::Graphics
{
	class StaticMesh
	{
	public:
		struct BoundingSphere
		{
			glm::vec3 center;
			float radius;
		};

		struct Meshlet
		{
			BoundingSphere bounds;
			int8_t coneAxis[3];
			int8_t coneCutoff;
			uint32_t vertexOffset;
			uint32_t primitiveOffset;
			uint8_t vertexCount;
			uint8_t primitiveCount;
		};

		struct MeshData
		{
			Bindless::DescriptorHandle vertexBuffer;
			Bindless::DescriptorHandle indexBuffer;
			Bindless::DescriptorHandle meshletBuffer;
			Bindless::DescriptorHandle meshletVertexBuffer;
			Bindless::DescriptorHandle meshletPrimitiveBuffer;
			uint32_t vertexCount;
			uint32_t indexCount;
			uint32_t meshletCount;
			BoundingSphere bounds;
		};

		struct CreateInfo
		{
			std::vector<Vertex> vertices;
			std::vector<uint32_t> indices;
			std::vector<Meshlet> meshlets;
			std::vector<uint32_t> vertexIndices;
			std::vector<uint8_t> primitiveIndices;
			BoundingSphere bounds;
		};

		StaticMesh(const CreateInfo& info) :
			m_vertexBuffer{ Buffer::vertexBuffer(sizeof(Vertex) * info.vertices.size(), 1, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) },
			m_indexBuffer{ Buffer::indexBuffer(sizeof(uint32_t) * info.indices.size(), 1, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) },
			m_meshletBuffer{ Buffer::storageBuffer(sizeof(Meshlet) * info.meshlets.size()) },
			m_meshletVertexBuffer{ Buffer::storageBuffer(sizeof(uint32_t) * info.vertexIndices.size()) },
			m_meshletPrimitiveBuffer{ Buffer::storageBuffer(sizeof(uint8_t) * info.primitiveIndices.size()) },
			m_meshDataBuffer{ Buffer::uniformBuffer(sizeof(MeshData)) },
			m_vertexCount{ static_cast<uint32_t>(info.vertices.size()) },
			m_indexCount{ static_cast<uint32_t>(info.indices.size()) },
			m_meshletCount{ static_cast<uint32_t>(info.meshlets.size()) },
			m_meshletIndexCount{ static_cast<uint32_t>(info.vertexIndices.size()) },
			m_meshletPrimitiveCount{ static_cast<uint32_t>(info.primitiveIndices.size()) }
		{
			m_vertexBuffer.buffer().upload(info.vertices);
			m_indexBuffer.buffer().upload(info.indices);
			m_meshletBuffer.buffer().upload(info.meshlets);
			m_meshletVertexBuffer.buffer().upload(info.vertexIndices);
			m_meshletPrimitiveBuffer.buffer().upload(info.primitiveIndices);

			MeshData meshData{
				.vertexBuffer = m_vertexBuffer.handle(),
				.indexBuffer = m_indexBuffer.handle(),
				.meshletBuffer = m_meshletBuffer.handle(),
				.meshletVertexBuffer = m_meshletVertexBuffer.handle(),
				.meshletPrimitiveBuffer = m_meshletPrimitiveBuffer.handle(),
				.vertexCount = m_vertexCount,
				.indexCount = m_indexCount,
				.meshletCount = m_meshletCount,
				.bounds = info.bounds
			};
			AGX_ASSERT_X(meshData.vertexBuffer.isValid(), "Invalid vertex buffer handle in StaticMesh!");
			AGX_ASSERT_X(meshData.meshletBuffer.isValid(), "Invalid meshlet buffer handle in StaticMesh!");
			AGX_ASSERT_X(meshData.meshletVertexBuffer.isValid(), "Invalid meshlet index buffer handle in StaticMesh!");
			AGX_ASSERT_X(meshData.meshletPrimitiveBuffer.isValid(), "Invalid meshlet primitive buffer handle in StaticMesh!");
			m_meshDataBuffer.buffer().singleWrite(&meshData, sizeof(MeshData), 0);

			Tools::setDebugUtilsObjectName(m_vertexBuffer.buffer(), "StaticMesh Vertices");
			Tools::setDebugUtilsObjectName(m_indexBuffer.buffer(), "StaticMesh Indices");
			Tools::setDebugUtilsObjectName(m_meshletBuffer.buffer(), "StaticMesh Meshlets");
			Tools::setDebugUtilsObjectName(m_meshletVertexBuffer.buffer(), "StaticMesh Meshlet Vertices");
			Tools::setDebugUtilsObjectName(m_meshletPrimitiveBuffer.buffer(), "StaticMesh Meshlet Primitives");
			Tools::setDebugUtilsObjectName(m_meshDataBuffer.buffer(), "StaticMesh Mesh Data");
		}

		StaticMesh(const StaticMesh&) = delete;
		StaticMesh(StaticMesh&&) = default;
		~StaticMesh() = default;

		auto operator=(const StaticMesh&) -> StaticMesh & = delete;
		auto operator=(StaticMesh&&) -> StaticMesh & = default;

		[[nodiscard]] auto vertexCount() const -> uint32_t { return m_vertexCount; }
		[[nodiscard]] auto indexCount() const -> uint32_t { return m_indexCount; }
		[[nodiscard]] auto meshletCount() const -> uint32_t { return m_meshletCount; }
		[[nodiscard]] auto meshDataBuffer() const -> const Bindless::BindlessBuffer& { return m_meshDataBuffer; }

		void draw(VkCommandBuffer cmd) const
		{
			VkBuffer vertexBuffers[] = { m_vertexBuffer.buffer() };
			VkDeviceSize offsets[] = { 0 };
			vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
			vkCmdBindIndexBuffer(cmd, m_indexBuffer.buffer(), 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(cmd, m_indexCount, 1, 0, 0, 0);
		}

		void drawMeshlets(VkCommandBuffer cmd) const
		{
			constexpr uint32_t MESHLETS_PER_GROUP = 1;
			uint32_t groupCount = (m_meshletCount + MESHLETS_PER_GROUP - 1) / MESHLETS_PER_GROUP;
			vkCmdDrawMeshTasksEXT(cmd, groupCount, 1, 1);
		}

	private:
		Bindless::BindlessBuffer m_meshDataBuffer;
		Bindless::BindlessBuffer m_vertexBuffer;
		Bindless::BindlessBuffer m_indexBuffer;
		Bindless::BindlessBuffer m_meshletBuffer;
		Bindless::BindlessBuffer m_meshletVertexBuffer;
		Bindless::BindlessBuffer m_meshletPrimitiveBuffer;

		uint32_t m_vertexCount;
		uint32_t m_indexCount;
		uint32_t m_meshletCount;
		uint32_t m_meshletIndexCount;
		uint32_t m_meshletPrimitiveCount;
	};
}