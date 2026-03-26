module;

#include "core/assert.h"

#include "scene/components.h"
#include "scene/scene.h"
#include "scene/system.h"

export module Aegis.Scene.Systems.CameraSystem;

import Aegis.Math;

export namespace Aegis::Scene
{
	class CameraSystem : public System
	{
	public:
		virtual void onUpdate(float deltaSeconds, Scene& scene) override
		{
			auto view = scene.registry().view<GlobalTransform, Camera>();
			for (auto&& [entity, transform, camera] : view.each())
			{
				calcViewMatrix(camera, transform);
				calcPerspectiveProjection(camera);
			}
		}

	private:
		void calcViewMatrix(Camera& camera, GlobalTransform& transform)
		{
			// Calculate the view matrix based on the camera's transform
			// see: https://www.3dgep.com/understanding-the-view-matrix/

			// Vectors for the view coordinate system
			const glm::vec3 f(glm::normalize(transform.forward()));				// Forward
			const glm::vec3 s(glm::normalize(glm::cross(f, Math::World::UP)));	// Sideways
			const glm::vec3 u(glm::cross(s, f));								// Up

			glm::mat4 viewMatrix{ 1.0f };
			viewMatrix[0][0] = s.x;
			viewMatrix[1][0] = s.y;
			viewMatrix[2][0] = s.z;
			viewMatrix[0][1] = u.x;
			viewMatrix[1][1] = u.y;
			viewMatrix[2][1] = u.z;
			viewMatrix[0][2] = -f.x;
			viewMatrix[1][2] = -f.y;
			viewMatrix[2][2] = -f.z;
			viewMatrix[3][0] = -glm::dot(s, transform.location);
			viewMatrix[3][1] = -glm::dot(u, transform.location);
			viewMatrix[3][2] = glm::dot(f, transform.location);
			camera.viewMatrix = viewMatrix;

			glm::mat4 inverseViewMat{ 1.0f };
			inverseViewMat[0][0] = s.x;
			inverseViewMat[0][1] = s.y;
			inverseViewMat[0][2] = s.z;
			inverseViewMat[1][0] = u.x;
			inverseViewMat[1][1] = u.y;
			inverseViewMat[1][2] = u.z;
			inverseViewMat[2][0] = f.x;
			inverseViewMat[2][1] = f.y;
			inverseViewMat[2][2] = f.z;
			inverseViewMat[3][0] = transform.location.x;
			inverseViewMat[3][1] = transform.location.y;
			inverseViewMat[3][2] = transform.location.z;
			camera.inverseViewMatrix = inverseViewMat;
		}

		void calcPerspectiveProjection(Camera& camera)
		{
			AGX_ASSERT(abs(camera.aspect - std::numeric_limits<float>::epsilon()) > 0.0f);

			// Projection matrix for right handed system with depth range [0, 1]
			// Note: Value [1][1] is negated because Vulkan uses a flipped y-axis
			const float tanHalfFovy = tan(camera.fov / 2.0f);
			glm::mat4 perspective{ 0.0f };
			perspective[0][0] = 1.0f / (camera.aspect * tanHalfFovy);
			perspective[1][1] = -1.0f / (tanHalfFovy);
			perspective[2][2] = camera.far / (camera.near - camera.far);
			perspective[2][3] = -1.0f;
			perspective[3][2] = -(camera.far * camera.near) / (camera.far - camera.near);
			camera.projectionMatrix = perspective;
		}
	};
}