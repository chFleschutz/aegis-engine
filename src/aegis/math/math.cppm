module;

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/matrix_major_storage.hpp>
#include <glm/gtx/vector_angle.hpp>

#include <cmath>

export module Aegis.Math;
export import :Interpolation;
export import :PerlinNoise;
export import :Random;

export namespace glm
{
	using glm::vec2;
	using glm::vec3;
	using glm::vec4;

	using glm::quat;

	using glm::mat2;
	using glm::mat3;
	using glm::mat4;
	using glm::mat2x2;
	using glm::mat2x3;
	using glm::mat2x4;
	using glm::mat3x2;
	using glm::mat3x3;
	using glm::mat3x4;
	using glm::mat4x4;

	using glm::angleAxis;
	using glm::clamp;
	using glm::cross;
	using glm::degrees;
	using glm::dot;
	using glm::eulerAngles;
	using glm::inverse;
	using glm::length;
	using glm::make_mat4;
	using glm::make_vec3;
	using glm::mod;
	using glm::normalize;
	using glm::radians;
	using glm::row;
	using glm::rowMajor4;
	using glm::two_pi;
	using glm::value_ptr;

	using glm::operator+;
	using glm::operator-;
	using glm::operator*;
	using glm::operator/;
}

export namespace Aegis::Math
{
	namespace World
	{
		constexpr glm::vec3 RIGHT = { 1.0f, 0.0f, 0.0f };
		constexpr glm::vec3 FORWARD = { 0.0f, 1.0f, 0.0f };
		constexpr glm::vec3 UP = { 0.0f, 0.0f, 1.0f };
	}

	/// @brief Returns the forward direction of a rotation
	auto forward(const glm::vec3& rotation) -> glm::vec3
	{
		const float sx = glm::sin(rotation.x);
		const float cx = glm::cos(rotation.x);
		const float sy = glm::sin(rotation.y);
		const float cy = glm::cos(rotation.y);
		const float cz = glm::cos(rotation.z);
		const float sz = glm::sin(rotation.z);
		return { sx * sy * cz - cx * sz, sx * sy * sz + cx * cz, sx * cy };
	}

	auto forward(const glm::quat& rotation) -> glm::vec3
	{
		return rotation * World::FORWARD;
	}

	/// @brief Returns the right direction of a rotation
	auto right(const glm::vec3& rotation) -> glm::vec3
	{
		const float sy = glm::sin(rotation.y);
		const float cy = glm::cos(rotation.y);
		const float cz = glm::cos(rotation.z);
		const float sz = glm::sin(rotation.z);
		return { cy * cz, cy * sz, -sy };
	}

	auto right(const glm::quat& rotation) -> glm::vec3
	{
		return rotation * World::RIGHT;
	}

	/// @brief Returns the up direction of a rotation
	auto up(const glm::vec3& rotation) -> glm::vec3
	{
		const float sx = glm::sin(rotation.x);
		const float cx = glm::cos(rotation.x);
		const float sy = glm::sin(rotation.y);
		const float cy = glm::cos(rotation.y);
		const float cz = glm::cos(rotation.z);
		const float sz = glm::sin(rotation.z);
		return { cx * sy * cz + sx * sz, cx * sy * sz - sx * cz, cx * cy };
	}

	auto up(const glm::quat& rotation) -> glm::vec3
	{
		return rotation * World::UP;
	}

	/// @brief Composes a transformation matrix from location, rotation and scale
	auto tranformationMatrix(const glm::vec3& location, const glm::vec3& rotation, const glm::vec3& scale) -> glm::mat4
	{
		const float c3 = glm::cos(rotation.z);
		const float s3 = glm::sin(rotation.z);
		const float c2 = glm::cos(rotation.x);
		const float s2 = glm::sin(rotation.x);
		const float c1 = glm::cos(rotation.y);
		const float s1 = glm::sin(rotation.y);
		return glm::mat4{
			{
				scale.x * (c1 * c3 + s1 * s2 * s3),
				scale.x * (c2 * s3),
				scale.x * (c1 * s2 * s3 - c3 * s1),
				0.0f,
			},
			{
				scale.y * (c3 * s1 * s2 - c1 * s3),
				scale.y * (c2 * c3),
				scale.y * (c1 * c3 * s2 + s1 * s3),
				0.0f,
			},
			{
				scale.z * (c2 * s1),
				scale.z * (-s2),
				scale.z * (c1 * c2),
				0.0f,
			},
			{
				location.x, location.y, location.z, 1.0f
			} };
	}

	auto tranformationMatrix(const glm::vec3& location, const glm::quat& rotation, const glm::vec3& scale) -> glm::mat4
	{
		glm::mat3 rotMat = glm::mat3_cast(rotation);
		rotMat[0] *= scale.x;
		rotMat[1] *= scale.y;
		rotMat[2] *= scale.z;

		return glm::mat4{
			glm::vec4{ rotMat[0], 0.0f },
			glm::vec4{ rotMat[1], 0.0f },
			glm::vec4{ rotMat[2], 0.0f },
			glm::vec4{ location, 1.0f }
		};
	}

	/// @brief Composes a matrix to transform normals into world space
	auto normalMatrix(const glm::vec3& rotation, const glm::vec3& scale) -> glm::mat3
	{
		const float c3 = glm::cos(rotation.z);
		const float s3 = glm::sin(rotation.z);
		const float c2 = glm::cos(rotation.x);
		const float s2 = glm::sin(rotation.x);
		const float c1 = glm::cos(rotation.y);
		const float s1 = glm::sin(rotation.y);
		const glm::vec3 invScale = 1.0f / scale;

		return glm::mat3{
			{
				invScale.x * (c1 * c3 + s1 * s2 * s3),
				invScale.x * (c2 * s3),
				invScale.x * (c1 * s2 * s3 - c3 * s1),
			},
			{
				invScale.y * (c3 * s1 * s2 - c1 * s3),
				invScale.y * (c2 * c3),
				invScale.y * (c1 * c3 * s2 + s1 * s3),
			},
			{
				invScale.z * (c2 * s1),
				invScale.z * (-s2),
				invScale.z * (c1 * c2),
			} };
	}

	auto normalMatrix(const glm::quat& rotation, const glm::vec3& scale) -> glm::mat3
	{
		glm::mat3 rotMat = glm::mat3_cast(rotation);
		rotMat[0] *= 1.0f / scale.x;
		rotMat[1] *= 1.0f / scale.y;
		rotMat[2] *= 1.0f / scale.z;
		return rotMat;
	}

	/// @brief Decomposes a transformation matrix into location, rotation and scale
	void decomposeTRS(const glm::mat4& matrix, glm::vec3& translation, glm::vec3& rotation, glm::vec3& scale)
	{
		translation = glm::vec3(matrix[3]);
		scale.x = glm::length(glm::vec3(matrix[0]));
		scale.y = glm::length(glm::vec3(matrix[1]));
		scale.z = glm::length(glm::vec3(matrix[2]));
		glm::mat3 rotationMat = glm::mat3(matrix);
		rotationMat[0] /= scale.x;
		rotationMat[1] /= scale.y;
		rotationMat[2] /= scale.z;
		rotation = glm::eulerAngles(glm::quat_cast(rotationMat));
	}

	void decomposeTRS(const glm::mat4& matrix, glm::vec3& translation, glm::quat& rotation, glm::vec3& scale)
	{
		translation = glm::vec3(matrix[3]);
		scale.x = glm::length(glm::vec3(matrix[0]));
		scale.y = glm::length(glm::vec3(matrix[1]));
		scale.z = glm::length(glm::vec3(matrix[2]));
		glm::mat3 rotationMat = glm::mat3(matrix);
		rotationMat[0] /= scale.x;
		rotationMat[1] /= scale.y;
		rotationMat[2] /= scale.z;
		rotation = glm::quat_cast(rotationMat);
	}

	/// @brief Checks if the target is in the field of view of the view direction
	auto inFOV(const glm::vec3& viewDirection, const glm::vec3& targetDirection, float fov) -> bool
	{
		return glm::angle(viewDirection, targetDirection) < 0.5f * fov;
	}
}
