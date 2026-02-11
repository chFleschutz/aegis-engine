#include "pch.h"
#include "static_mesh.h"

#include "graphics/vulkan/volk_include.h"
#include "graphics/vulkan/vulkan_tools.h"

namespace Aegis::Graphics
{
	auto StaticMesh::bindingDescription() -> VkVertexInputBindingDescription
	{
		static VkVertexInputBindingDescription bindingDescription{
			.binding = 0,
			.stride = sizeof(Vertex),
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
		};
		return bindingDescription;
	}

	auto StaticMesh::attributeDescriptions() -> std::vector<VkVertexInputAttributeDescription>
	{
		static auto attributeDescriptions = std::vector{
			VkVertexInputAttributeDescription{
				.location = 0,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = offsetof(Vertex, position)
			},
			VkVertexInputAttributeDescription{
				.location = 1,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = offsetof(Vertex, normal)
			},
			VkVertexInputAttributeDescription{
				.location = 2,
				.binding = 0,
				.format = VK_FORMAT_R32G32_SFLOAT,
				.offset = offsetof(Vertex, uv)
			},
			VkVertexInputAttributeDescription{
				.location = 3,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = offsetof(Vertex, color)
			}
		};
		return attributeDescriptions;
	}

	StaticMesh::StaticMesh(const CreateInfo& info) :
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

		Tools::vk::setDebugUtilsObjectName(m_vertexBuffer.buffer(), "StaticMesh Vertices");
		Tools::vk::setDebugUtilsObjectName(m_indexBuffer.buffer(), "StaticMesh Indices");
		Tools::vk::setDebugUtilsObjectName(m_meshletBuffer.buffer(), "StaticMesh Meshlets");
		Tools::vk::setDebugUtilsObjectName(m_meshletVertexBuffer.buffer(), "StaticMesh Meshlet Vertices");
		Tools::vk::setDebugUtilsObjectName(m_meshletPrimitiveBuffer.buffer(), "StaticMesh Meshlet Primitives");
		Tools::vk::setDebugUtilsObjectName(m_meshDataBuffer.buffer(), "StaticMesh Mesh Data");
	}

	void StaticMesh::draw(VkCommandBuffer cmd) const
	{
		VkBuffer vertexBuffers[] = { m_vertexBuffer.buffer() };
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
		vkCmdBindIndexBuffer(cmd, m_indexBuffer.buffer(), 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cmd, m_indexCount, 1, 0, 0, 0);
	}

	void StaticMesh::drawMeshlets(VkCommandBuffer cmd) const
	{
		constexpr uint32_t MESHLETS_PER_GROUP = 1;
		uint32_t groupCount = (m_meshletCount + MESHLETS_PER_GROUP - 1) / MESHLETS_PER_GROUP;
		vkCmdDrawMeshTasksEXT(cmd, groupCount, 1, 1);
	}
}