module aegis.editor.core;
import std;
import aegis.math;

namespace aegis::editor
{
auto linearToSrgb(float linear) -> float
{
    const double value = linear;
    if (value <= 0.0031308)
        return static_cast<float>(value * 12.92);
    return static_cast<float>(1.055 * std::pow(value, 1.0 / 2.4) - 0.055);
}

auto srgbToLinear(float srgb) -> float
{
    const double value = srgb;
    if (value <= 0.04045)
        return static_cast<float>(value / 12.92);
    return static_cast<float>(std::pow((value + 0.055) / 1.055, 2.4));
}

auto linearToSrgb(const math::Float3& linear) -> math::Float3
{
    return { linearToSrgb(linear.x), linearToSrgb(linear.y), linearToSrgb(linear.z) };
}

auto linearAfterEdit(const math::Float3& stored, const math::Float3& shown, const math::Float3& edited) -> math::Float3
{
    math::Float3 result = stored;
    for (int i = 0; i < 3; ++i)
    {
        if (edited[i] != shown[i])
            result[i] = srgbToLinear(edited[i]);
    }
    return result;
}

auto iconTint(const math::Float3& linear) -> math::Float3
{
    constexpr float whiteShare{ 0.25f };
    const float brightest = std::max({ linear.x, linear.y, linear.z });
    if (!(brightest > 0.0f))
        return math::Float3{ 1.0f };
    math::Float3 tint;
    for (int i = 0; i < 3; ++i)
        tint[i] = std::lerp(linearToSrgb(std::max(linear[i] / brightest, 0.0f)), 1.0f, whiteShare);
    return tint;
}
}
