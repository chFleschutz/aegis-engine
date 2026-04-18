module;

#include "core/assert.h"

#include <meshoptimizer.h>

#include <vector>

export module Aegis.Graphics.MeshPreprocessor;

import Aegis.Math;
import Aegis.Graphics.StaticMesh;

export namespace Aegis::Graphics
{
	class MeshPreprocessor
	{
	public:
		struct Input
		{
			std::vector<glm::vec3> positions; // Required
			std::vector<glm::vec3> normals;   // Required
			std::vector<glm::vec2> uvs;
			std::vector<glm::vec3> colors;
			std::vector<uint32_t> indices;

			float overdrawThreshold = 1.05f;

			std::size_t maxVerticesPerMeshlet = 64;
			std::size_t maxTrianglesPerMeshlet = 126;
			float coneWeight = 0;
		};

		static auto process(Input& input) -> StaticMesh::CreateInfo
		{
			std::vector<Vertex> vertices = interleave(input);
			std::vector<uint32_t> indices = std::move(input.indices);
			std::size_t initialVertexCount = vertices.size();
			bool hasIndices = !indices.empty();
			std::size_t indexCount = hasIndices ? indices.size() : initialVertexCount;

			// Vertex and Index remapping

			std::vector<uint32_t> remap(initialVertexCount);
			std::size_t vertexCount = meshopt_generateVertexRemap(
				remap.data(),
				hasIndices ? indices.data() : nullptr,
				hasIndices ? indices.size() : initialVertexCount,
				vertices.data(),
				initialVertexCount,
				sizeof(Vertex));

			indices.resize(indexCount);
			meshopt_remapIndexBuffer(
				indices.data(),
				hasIndices ? indices.data() : nullptr,
				hasIndices ? indices.size() : initialVertexCount,
				remap.data());

			meshopt_remapVertexBuffer(
				vertices.data(),
				vertices.data(),
				initialVertexCount,
				sizeof(Vertex),
				remap.data());

			// Optimizations

			meshopt_optimizeVertexCache(
				indices.data(),
				indices.data(),
				indices.size(),
				vertices.size());

			meshopt_optimizeOverdraw(
				indices.data(),
				indices.data(),
				indices.size(),
				&vertices[0].position.x,
				vertices.size(),
				sizeof(Vertex),
				input.overdrawThreshold);

			meshopt_optimizeVertexFetch(
				vertices.data(),
				indices.data(),
				indices.size(),
				vertices.data(),
				vertices.size(),
				sizeof(Vertex));

			// Bounding sphere

			meshopt_Bounds bounds = meshopt_computeSphereBounds(
				&vertices[0].position.x,
				vertices.size(),
				sizeof(Vertex),
				nullptr,
				0);

			StaticMesh::BoundingSphere meshBounds{
				.center = { bounds.center[0], bounds.center[1], bounds.center[2] },
				.radius = bounds.radius,
			};

			// Meshlet generation

			std::size_t maxMeshlets = meshopt_buildMeshletsBound(
				indices.size(),
				input.maxVerticesPerMeshlet,
				input.maxTrianglesPerMeshlet);

			std::vector<meshopt_Meshlet> meshoptMeshlets(maxMeshlets);
			std::vector<uint32_t> meshletVertices(indexCount);
			std::vector<uint8_t> meshletPrimitives(indexCount);

			std::size_t meshletCount = meshopt_buildMeshlets(
				meshoptMeshlets.data(),
				meshletVertices.data(),
				meshletPrimitives.data(),
				indices.data(),
				indices.size(),
				&vertices[0].position.x,
				vertices.size(),
				sizeof(Vertex),
				input.maxVerticesPerMeshlet,
				input.maxTrianglesPerMeshlet,
				input.coneWeight);

			const auto& lastMeshlet = meshoptMeshlets[meshletCount - 1];
			meshoptMeshlets.resize(meshletCount);
			meshletVertices.resize(lastMeshlet.vertex_offset + lastMeshlet.vertex_count);
			meshletPrimitives.resize(lastMeshlet.triangle_offset + lastMeshlet.triangle_count * 3);

			std::vector<StaticMesh::Meshlet> meshlets;
			meshlets.reserve(meshletCount);
			for (auto& meshlet : meshoptMeshlets)
			{
				meshopt_optimizeMeshlet(
					&meshletVertices[meshlet.vertex_offset],
					&meshletPrimitives[meshlet.triangle_offset],
					meshlet.triangle_count,
					meshlet.vertex_count);

				meshopt_Bounds bounds = meshopt_computeMeshletBounds(
					&meshletVertices[meshlet.vertex_offset],
					&meshletPrimitives[meshlet.triangle_offset],
					meshlet.triangle_count,
					&vertices[0].position.x,
					vertices.size(),
					sizeof(Vertex));

				meshlets.emplace_back(StaticMesh::Meshlet{
					.bounds = { glm::vec3{ bounds.center[0], bounds.center[1], bounds.center[2] }, bounds.radius },
					.coneAxis = { bounds.cone_axis_s8[0], bounds.cone_axis_s8[1], bounds.cone_axis_s8[2] },
					.coneCutoff = bounds.cone_cutoff_s8,
					.vertexOffset = meshlet.vertex_offset,
					.primitiveOffset = meshlet.triangle_offset,
					.vertexCount = static_cast<uint8_t>(meshlet.vertex_count),
					.primitiveCount = static_cast<uint8_t>(meshlet.triangle_count),
					});
			}

			// Fill CreateInfo

			return StaticMesh::CreateInfo{
				.vertices = std::move(vertices),
				.indices = std::move(indices),
				.meshlets = std::move(meshlets),
				.vertexIndices = std::move(meshletVertices),
				.primitiveIndices = std::move(meshletPrimitives),
				.bounds = meshBounds
			};
		}

	private:
		static auto interleave(const Input& input) -> std::vector<Vertex>
		{
			AGX_ASSERT_X(input.positions.size() == input.normals.size(), "Positions and normals size mismatch");

			std::vector<Vertex> vertices(input.positions.size());
			for (std::size_t i = 0; i < input.positions.size(); i++)
			{
				vertices[i].position = input.positions[i];
				vertices[i].normal = input.normals[i];
				vertices[i].uv = (i < input.uvs.size()) ? input.uvs[i] : glm::vec2{ 0.0f };
				vertices[i].color = (i < input.colors.size()) ? input.colors[i] : glm::vec3{ 1.0f };
			}
			return vertices;
		}
	};
}