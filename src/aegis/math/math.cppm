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

export module aegis.math;
import std;
export import :random;

export namespace glm
{
using glm::vec2;
using glm::vec3;
using glm::vec4;

using glm::quat;

using glm::mat2;
using glm::mat2x2;
using glm::mat2x3;
using glm::mat2x4;
using glm::mat3;
using glm::mat3x2;
using glm::mat3x3;
using glm::mat3x4;
using glm::mat4;
using glm::mat4x4;

using glm::angleAxis;
using glm::clamp;
using glm::cross;
using glm::degrees;
using glm::dot;
using glm::eulerAngles;
using glm::inverse;
using glm::length;
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

export namespace aegis::math
{
namespace world
{
constexpr glm::vec3 RIGHT = { 1.0f, 0.0f, 0.0f };
constexpr glm::vec3 FORWARD = { 0.0f, 1.0f, 0.0f };
constexpr glm::vec3 UP = { 0.0f, 0.0f, 1.0f };
}

/// @brief Returns the forward direction of a rotation
auto forward(const glm::quat& rotation) -> glm::vec3
{
    return rotation * world::FORWARD;
}

/// @brief Returns the right direction of a rotation
auto right(const glm::quat& rotation) -> glm::vec3
{
    return rotation * world::RIGHT;
}

/// @brief Returns the up direction of a rotation
auto up(const glm::quat& rotation) -> glm::vec3
{
    return rotation * world::UP;
}

/// @brief Composes a transformation matrix from location, rotation and scale
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

/// @brief Decomposes a transformation matrix into location, rotation and scale
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
}
