module aegis.render_passes;
import std;
import aegis.math;

namespace aegis::render_graph::detail
{
auto viewDepth(const glm::mat4& view, const glm::vec3& point) -> float
{
    return -(view * glm::vec4{ point, 1.0f }).z;
}

auto worldCenter(const glm::mat3x4& model, const glm::vec3& localCenter) -> glm::vec3
{
    const glm::vec4 local{ localCenter, 1.0f };
    return { glm::dot(math::row(model, 0), local), glm::dot(math::row(model, 1), local), glm::dot(math::row(model, 2), local) };
}

auto sortBackToFront(std::span<DepthSortedDraw> draws) -> void
{
    std::ranges::stable_sort(draws, std::ranges::greater{}, &DepthSortedDraw::depth);
}
}
