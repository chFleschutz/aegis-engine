module aegis.render_passes;
import std;
import aegis.math;

namespace aegis::render_graph::detail
{
auto viewDepth(const math::Float4x4& view, const math::Float3& point) -> float
{
    return -(view * math::Float4{ point, 1.0f }).z;
}

auto worldCenter(const math::Float3x4& model, const math::Float3& localCenter) -> math::Float3
{
    const math::Float4 local{ localCenter, 1.0f };
    return { math::dot(model.row(0), local), math::dot(model.row(1), local), math::dot(model.row(2), local) };
}

auto sortBackToFront(std::span<DepthSortedDraw> draws) -> void
{
    std::ranges::stable_sort(draws, std::ranges::greater{}, &DepthSortedDraw::depth);
}
}
