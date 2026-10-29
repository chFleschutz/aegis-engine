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

auto linearToSrgb(const glm::vec3& linear) -> glm::vec3
{
    return { linearToSrgb(linear.x), linearToSrgb(linear.y), linearToSrgb(linear.z) };
}

auto linearAfterEdit(const glm::vec3& stored, const glm::vec3& shown, const glm::vec3& edited) -> glm::vec3
{
    glm::vec3 result = stored;
    for (int i = 0; i < 3; ++i)
    {
        if (edited[i] != shown[i])
            result[i] = srgbToLinear(edited[i]);
    }
    return result;
}
}
