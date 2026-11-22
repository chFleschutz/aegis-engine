export module aegis.math:scalar;

import std;

export namespace aegis::math
{
/// @brief A type a vector, matrix or quaternion can hold: any integer or floating-point type except bool
template<typename T>
concept Scalar = std::is_arithmetic_v<T> && !std::same_as<T, bool>;

/// @brief A vector's component count or a matrix's row or column count: 2, 3 or 4
template<std::size_t N>
concept Dimension = N >= 2 && N <= 4;

template<std::floating_point T>
constexpr T pi = std::numbers::pi_v<T>;

template<std::floating_point T>
constexpr T twoPi = T{ 2 } * std::numbers::pi_v<T>;

/// @brief The tolerance nearlyEqual uses when none is given: 1e-5 for float, 1e-12 for wider types
template<std::floating_point T>
constexpr T defaultTolerance = std::same_as<T, float> ? T(1e-5) : T(1e-12);

template<std::floating_point T>
[[nodiscard]] constexpr auto radians(T degrees) noexcept -> T
{
    return degrees * (pi<T> / T{ 180 });
}

template<std::floating_point T>
[[nodiscard]] constexpr auto degrees(T radians) noexcept -> T
{
    return radians * (T{ 180 } / pi<T>);
}

template<Scalar T>
[[nodiscard]] constexpr auto min(T a, T b) noexcept -> T
{
    return b < a ? b : a;
}

template<Scalar T>
[[nodiscard]] constexpr auto max(T a, T b) noexcept -> T
{
    return a < b ? b : a;
}

template<Scalar T>
[[nodiscard]] constexpr auto abs(T x) noexcept -> T
{
    if constexpr (std::unsigned_integral<T>)
        return x;
    else
        return x < T{ 0 } ? static_cast<T>(-x) : x;
}

template<Scalar T>
[[nodiscard]] constexpr auto clamp(T x, T low, T high) noexcept -> T
{
    return min(max(x, low), high);
}

/// @brief Interpolates from `a` (t = 0) to `b` (t = 1), exactly at both ends
template<std::floating_point T>
[[nodiscard]] constexpr auto lerp(T a, T b, T t) noexcept -> T
{
    return a * (T{ 1 } - t) + b * t;
}

/// @brief Clamps to [0, 1]
template<std::floating_point T>
[[nodiscard]] constexpr auto saturate(T x) noexcept -> T
{
    return clamp(x, T{ 0 }, T{ 1 });
}

template<std::floating_point T>
[[nodiscard]] auto floor(T x) noexcept -> T
{
    return std::floor(x);
}

/// @brief The fractional part, `x - floor(x)`: in [0, 1) also for negative `x`
template<std::floating_point T>
[[nodiscard]] auto fract(T x) noexcept -> T
{
    return x - std::floor(x);
}

template<std::floating_point T>
[[nodiscard]] auto sqrt(T x) noexcept -> T
{
    return std::sqrt(x);
}

/// @brief Floored modulo, `x - y * floor(x / y)` (glm's, not Slang's fmod): takes the sign of `y`
template<std::floating_point T>
[[nodiscard]] auto mod(T x, T y) noexcept -> T
{
    return x - y * std::floor(x / y);
}

/// @brief Whether `a` and `b` differ by at most `tolerance`, scaled by the larger magnitude once that exceeds 1
/// @note Absolute near zero, relative for large values: |a - b| <= tolerance * max(1, |a|, |b|).
template<std::floating_point T>
[[nodiscard]] constexpr auto nearlyEqual(T a, T b, T tolerance = defaultTolerance<T>) noexcept -> bool
{
    return abs(a - b) <= tolerance * max(T{ 1 }, max(abs(a), abs(b)));
}

/// @brief Converts an IEEE 754 half-precision value, given as its bits, to float
[[nodiscard]] constexpr auto halfToFloat(std::uint16_t half) noexcept -> float
{
    const std::uint32_t sign = (static_cast<std::uint32_t>(half) & 0x8000u) << 16;
    const std::uint32_t exponent = (static_cast<std::uint32_t>(half) >> 10) & 0x1Fu;
    const std::uint32_t mantissa = static_cast<std::uint32_t>(half) & 0x3FFu;

    if (exponent == 0x1Fu) // infinity or NaN
        return std::bit_cast<float>(sign | 0x7F800000u | (mantissa << 13));
    if (exponent != 0) // normal: rebias the exponent from 15 to 127
        return std::bit_cast<float>(sign | ((exponent + 112u) << 23) | (mantissa << 13));

    // zero or subnormal: mantissa * 2^-24, exact in float
    const float magnitude = static_cast<float>(mantissa) * 0x1p-24f;
    return sign != 0 ? -magnitude : magnitude;
}
}
