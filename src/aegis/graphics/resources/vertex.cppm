module;

#include "graphics/vulkan/vulkan_include.h"

#include <vector>

export module Aegis.Graphics.Vertex;

import Aegis.Math;

export namespace Aegis::Graphics
{
	struct Vertex
	{
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec2 uv;
		glm::vec3 color;

		static auto bindingDescription() -> VkVertexInputBindingDescription
		{
			static VkVertexInputBindingDescription bindingDescription{
				.binding = 0,
				.stride = sizeof(Vertex),
				.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
			};
			return bindingDescription;
		}

		static auto attributeDescriptions() -> std::vector<VkVertexInputAttributeDescription>
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
	};
}