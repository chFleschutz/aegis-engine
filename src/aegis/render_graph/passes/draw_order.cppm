export module aegis.render_passes:draw_order;
import std;
import aegis.math;

// The order of blended draws: alpha blending composes correctly only when what is behind is drawn first, so a pass
// that blends sorts its draws back to front by their depth in its view.

export namespace aegis::render_graph::detail
{
/// @brief One draw to order: its depth in the view (viewDepth) and its index in the caller's list of draws.
struct DepthSortedDraw
{
    float depth{ 0.0f };
    std::uint32_t index{ 0 };
};

/// @brief How far in front of the camera of 'view' (a right-handed view matrix, looking along -z) the world-space
///        'point' lies, in meters along its view direction; negative behind it.
[[nodiscard]] auto viewDepth(const glm::mat4& view, const glm::vec3& point) -> float;

/// @brief The world-space centre of a mesh's bounding sphere centred at 'localCenter', drawn with 'model' (the top
///        three rows of its model matrix, Instance::model).
[[nodiscard]] auto worldCenter(const glm::mat3x4& model, const glm::vec3& localCenter) -> glm::vec3;

/// @brief Sorts 'draws' back to front: the deepest first. Draws of equal depth keep their order.
auto sortBackToFront(std::span<DepthSortedDraw> draws) -> void;
}
