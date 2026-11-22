export module aegis.editor.core:color;
import std;
import aegis.math;

export namespace aegis::editor
{
/// @brief The sRGB encoding of a linear value, by the exact sRGB transfer function (IEC 61966-2-1). The curve
///        continues past 1 for HDR values, and below 0 on its linear segment.
[[nodiscard]] auto linearToSrgb(float linear) -> float;

/// @brief The linear value of an sRGB-encoded one: the inverse of linearToSrgb.
[[nodiscard]] auto srgbToLinear(float srgb) -> float;

/// @brief A linear color's channels as sRGB, the way the color picker shows them.
[[nodiscard]] auto linearToSrgb(const glm::vec3& linear) -> glm::vec3;

/// @brief The linear color after the picker changed 'shown' (the sRGB of 'stored') to 'edited'. A channel the pick
///        left alone keeps its stored value exactly, so touching one channel doesn't move the others by the
///        rounding of a round trip.
[[nodiscard]] auto linearAfterEdit(const glm::vec3& stored, const glm::vec3& shown, const glm::vec3& edited) -> glm::vec3;

/// @brief A light's linear color as its viewport icon shows it, in sRGB: at full brightness (the largest channel 1,
///        whatever the intensity) and mixed a quarter with white, so a dark or deeply saturated color still reads on
///        the icon's dark backdrop. Black or a negative color gives white.
[[nodiscard]] auto iconTint(const glm::vec3& linear) -> glm::vec3;
}
