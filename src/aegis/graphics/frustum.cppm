module;

#include "core/assert.h"

#include <array>

export module Aegis.Graphics.Frustum;

import Aegis.Math;

export namespace Aegis::Graphics
{
	struct Frustum
	{
		// Plane equation: Ax + By + Cz + D = 0  -> represented as { A, B, C, D }
		using Plane = glm::vec4;

		std::array<Plane, 6> planes;

		static auto extractFrom(const glm::mat4& viewProjection) -> Frustum
		{
			auto row0 = glm::row(viewProjection, 0);
			auto row1 = glm::row(viewProjection, 1);
			auto row2 = glm::row(viewProjection, 2);
			auto row3 = glm::row(viewProjection, 3);

			Frustum frustum{
				row3 + row0,
				row3 - row0,
				row3 + row1,
				row3 - row1,
				row2,	  // Note: Vulkan uses z depth range [0, 1]
				row3 - row2,
			};

			// Normalize planes
			for (auto& plane : frustum.planes)
			{
				float len = glm::length(glm::vec3(plane));
				AGX_ASSERT_X(len > 0.0f, "Frustum Extraction: Plane has zero length (Div by 0 hazard)");
				plane /= len;
			}

			return frustum;
		}
	};
}