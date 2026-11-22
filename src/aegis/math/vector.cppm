module;

#include <cassert>

export module aegis.math:vector;

import std;
import :scalar;

export namespace aegis::math
{
/// @brief A column vector of N components of type T, laid out as N consecutive T (no padding)
/// @note Default-constructs to zero. Component-wise operations are written once for every T and N (see
/// detail::map, zip, reduce); only the storage and constructors are per size.
template<Scalar T, std::size_t N>
    requires Dimension<N>
struct Vector;

template<Scalar T>
struct Vector<T, 2>
{
    using ValueType = T;
    static constexpr std::size_t componentCount = 2;

    T x{};
    T y{};

    constexpr Vector() noexcept = default;
    explicit constexpr Vector(T s) noexcept :
        x{ s },
        y{ s }
    {
    }
    constexpr Vector(T x, T y) noexcept :
        x{ x },
        y{ y }
    {
    }

    /// @brief Drops the components past y
    template<std::size_t M>
        requires(M > 2)
    explicit constexpr Vector(Vector<T, M> v) noexcept :
        x{ v.x },
        y{ v.y }
    {
    }

    /// @brief Converts each component to T
    template<Scalar U>
        requires(!std::same_as<U, T>)
    explicit constexpr Vector(Vector<U, 2> v) noexcept :
        x{ static_cast<T>(v.x) },
        y{ static_cast<T>(v.y) }
    {
    }

    [[nodiscard]] constexpr auto operator[](std::size_t i) noexcept -> T&
    {
        assert(i < componentCount);
        return i == 0 ? x : y;
    }

    [[nodiscard]] constexpr auto operator[](std::size_t i) const noexcept -> const T&
    {
        assert(i < componentCount);
        return i == 0 ? x : y;
    }

    friend constexpr auto operator==(const Vector&, const Vector&) noexcept -> bool = default;
};

template<Scalar T>
struct Vector<T, 3>
{
    using ValueType = T;
    static constexpr std::size_t componentCount = 3;

    T x{};
    T y{};
    T z{};

    constexpr Vector() noexcept = default;
    explicit constexpr Vector(T s) noexcept :
        x{ s },
        y{ s },
        z{ s }
    {
    }
    constexpr Vector(T x, T y, T z) noexcept :
        x{ x },
        y{ y },
        z{ z }
    {
    }
    constexpr Vector(Vector<T, 2> xy, T z) noexcept :
        x{ xy.x },
        y{ xy.y },
        z{ z }
    {
    }

    /// @brief Drops w
    explicit constexpr Vector(Vector<T, 4> v) noexcept :
        x{ v.x },
        y{ v.y },
        z{ v.z }
    {
    }

    /// @brief Converts each component to T
    template<Scalar U>
        requires(!std::same_as<U, T>)
    explicit constexpr Vector(Vector<U, 3> v) noexcept
        :
        x{ static_cast<T>(v.x) },
        y{ static_cast<T>(v.y) },
        z{ static_cast<T>(v.z) }
    {
    }

    [[nodiscard]] constexpr auto xy() const noexcept -> Vector<T, 2> { return { x, y }; }

    [[nodiscard]] constexpr auto operator[](std::size_t i) noexcept -> T&
    {
        assert(i < componentCount);
        return i == 0 ? x : (i == 1 ? y : z);
    }

    [[nodiscard]] constexpr auto operator[](std::size_t i) const noexcept -> const T&
    {
        assert(i < componentCount);
        return i == 0 ? x : (i == 1 ? y : z);
    }

    friend constexpr auto operator==(const Vector&, const Vector&) noexcept -> bool = default;
};

template<Scalar T>
struct Vector<T, 4>
{
    using ValueType = T;
    static constexpr std::size_t componentCount = 4;

    T x{};
    T y{};
    T z{};
    T w{};

    constexpr Vector() noexcept = default;
    explicit constexpr Vector(T s) noexcept :
        x{ s },
        y{ s },
        z{ s },
        w{ s }
    {
    }
    constexpr Vector(T x, T y, T z, T w) noexcept :
        x{ x },
        y{ y },
        z{ z },
        w{ w }
    {
    }
    constexpr Vector(Vector<T, 3> xyz, T w) noexcept :
        x{ xyz.x },
        y{ xyz.y },
        z{ xyz.z },
        w{ w }
    {
    }
    constexpr Vector(Vector<T, 2> xy, T z, T w) noexcept :
        x{ xy.x },
        y{ xy.y },
        z{ z },
        w{ w }
    {
    }

    /// @brief Converts each component to T
    template<Scalar U>
        requires(!std::same_as<U, T>)
    explicit constexpr Vector(Vector<U, 4> v) noexcept
        :
        x{ static_cast<T>(v.x) },
        y{ static_cast<T>(v.y) },
        z{ static_cast<T>(v.z) },
        w{ static_cast<T>(v.w) }
    {
    }

    [[nodiscard]] constexpr auto xy() const noexcept -> Vector<T, 2> { return { x, y }; }
    [[nodiscard]] constexpr auto xyz() const noexcept -> Vector<T, 3> { return { x, y, z }; }

    [[nodiscard]] constexpr auto operator[](std::size_t i) noexcept -> T&
    {
        assert(i < componentCount);
        return i == 0 ? x : (i == 1 ? y : (i == 2 ? z : w));
    }

    [[nodiscard]] constexpr auto operator[](std::size_t i) const noexcept -> const T&
    {
        assert(i < componentCount);
        return i == 0 ? x : (i == 1 ? y : (i == 2 ? z : w));
    }

    friend constexpr auto operator==(const Vector&, const Vector&) noexcept -> bool = default;
};

namespace detail
{
template<typename V>
struct IsVector : std::false_type
{
};

template<Scalar T, std::size_t N>
    requires Dimension<N>
struct IsVector<Vector<T, N>> : std::true_type
{
};
}

/// @brief Any Vector<T, N>
template<typename V>
concept VectorType = detail::IsVector<V>::value;

/// @brief A Vector<T, N> of a floating-point T
template<typename V>
concept FloatVector = VectorType<V> && std::floating_point<typename V::ValueType>;

namespace detail
{
// The one place that knows the vector sizes: every component-wise operation goes through these.

/// @brief The vector of f(0) ... f(N - 1)
template<Scalar T, std::size_t N, typename F>
[[nodiscard]] constexpr auto generate(F f) noexcept -> Vector<T, N>
{
    if constexpr (N == 2)
        return Vector<T, 2>{ f(std::size_t{ 0 }), f(std::size_t{ 1 }) };
    else if constexpr (N == 3)
        return Vector<T, 3>{ f(std::size_t{ 0 }), f(std::size_t{ 1 }), f(std::size_t{ 2 }) };
    else
        return Vector<T, 4>{ f(std::size_t{ 0 }), f(std::size_t{ 1 }), f(std::size_t{ 2 }), f(std::size_t{ 3 }) };
}

/// @brief The vector of f(v[i])
template<Scalar T, std::size_t N, typename F>
[[nodiscard]] constexpr auto map(Vector<T, N> v, F f) noexcept -> Vector<T, N>
{
    if constexpr (N == 2)
        return Vector<T, 2>{ f(v.x), f(v.y) };
    else if constexpr (N == 3)
        return Vector<T, 3>{ f(v.x), f(v.y), f(v.z) };
    else
        return Vector<T, 4>{ f(v.x), f(v.y), f(v.z), f(v.w) };
}

/// @brief The vector of f(a[i], b[i])
template<Scalar T, std::size_t N, typename F>
[[nodiscard]] constexpr auto zip(Vector<T, N> a, Vector<T, N> b, F f) noexcept -> Vector<T, N>
{
    if constexpr (N == 2)
        return Vector<T, 2>{ f(a.x, b.x), f(a.y, b.y) };
    else if constexpr (N == 3)
        return Vector<T, 3>{ f(a.x, b.x), f(a.y, b.y), f(a.z, b.z) };
    else
        return Vector<T, 4>{ f(a.x, b.x), f(a.y, b.y), f(a.z, b.z), f(a.w, b.w) };
}

/// @brief f folded over the components from the left: f(f(v.x, v.y), v.z) ...
template<Scalar T, std::size_t N, typename F>
[[nodiscard]] constexpr auto reduce(Vector<T, N> v, F f) noexcept -> T
{
    if constexpr (N == 2)
        return f(v.x, v.y);
    else if constexpr (N == 3)
        return f(f(v.x, v.y), v.z);
    else
        return f(f(f(v.x, v.y), v.z), v.w);
}
}

// Arithmetic: component-wise. A scalar operand takes the vector's T (std::type_identity_t), so `v * 2`
// deduces T from the vector only; vectors of different T never mix.

template<Scalar T, std::size_t N>
[[nodiscard]] constexpr auto operator+(Vector<T, N> a, Vector<T, N> b) noexcept -> Vector<T, N>
{
    return detail::zip(a, b, std::plus<T>{});
}

template<Scalar T, std::size_t N>
[[nodiscard]] constexpr auto operator-(Vector<T, N> a, Vector<T, N> b) noexcept -> Vector<T, N>
{
    return detail::zip(a, b, std::minus<T>{});
}

template<Scalar T, std::size_t N>
[[nodiscard]] constexpr auto operator*(Vector<T, N> a, Vector<T, N> b) noexcept -> Vector<T, N>
{
    return detail::zip(a, b, std::multiplies<T>{});
}

template<Scalar T, std::size_t N>
[[nodiscard]] constexpr auto operator/(Vector<T, N> a, Vector<T, N> b) noexcept -> Vector<T, N>
{
    return detail::zip(a, b, std::divides<T>{});
}

template<Scalar T, std::size_t N>
    requires std::signed_integral<T> || std::floating_point<T>
[[nodiscard]] constexpr auto operator-(Vector<T, N> v) noexcept -> Vector<T, N>
{
    return detail::map(v, std::negate<T>{});
}

template<Scalar T, std::size_t N>
[[nodiscard]] constexpr auto operator*(Vector<T, N> v, std::type_identity_t<T> s) noexcept -> Vector<T, N>
{
    return detail::map(v, [s](T c) -> T { return c * s; });
}

template<Scalar T, std::size_t N>
[[nodiscard]] constexpr auto operator*(std::type_identity_t<T> s, Vector<T, N> v) noexcept -> Vector<T, N>
{
    return detail::map(v, [s](T c) -> T { return s * c; });
}

template<Scalar T, std::size_t N>
[[nodiscard]] constexpr auto operator/(Vector<T, N> v, std::type_identity_t<T> s) noexcept -> Vector<T, N>
{
    return detail::map(v, [s](T c) -> T { return c / s; });
}

template<Scalar T, std::size_t N>
constexpr auto operator+=(Vector<T, N>& a, Vector<T, N> b) noexcept -> Vector<T, N>&
{
    return a = a + b;
}

template<Scalar T, std::size_t N>
constexpr auto operator-=(Vector<T, N>& a, Vector<T, N> b) noexcept -> Vector<T, N>&
{
    return a = a - b;
}

template<Scalar T, std::size_t N>
constexpr auto operator*=(Vector<T, N>& a, Vector<T, N> b) noexcept -> Vector<T, N>&
{
    return a = a * b;
}

template<Scalar T, std::size_t N>
constexpr auto operator/=(Vector<T, N>& a, Vector<T, N> b) noexcept -> Vector<T, N>&
{
    return a = a / b;
}

template<Scalar T, std::size_t N>
constexpr auto operator*=(Vector<T, N>& v, std::type_identity_t<T> s) noexcept -> Vector<T, N>&
{
    return v = v * s;
}

template<Scalar T, std::size_t N>
constexpr auto operator/=(Vector<T, N>& v, std::type_identity_t<T> s) noexcept -> Vector<T, N>&
{
    return v = v / s;
}

// Geometry

template<Scalar T, std::size_t N>
[[nodiscard]] constexpr auto dot(Vector<T, N> a, Vector<T, N> b) noexcept -> T
{
    return detail::reduce(a * b, std::plus<T>{});
}

template<Scalar T>
[[nodiscard]] constexpr auto cross(Vector<T, 3> a, Vector<T, 3> b) noexcept -> Vector<T, 3>
{
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

template<FloatVector V>
[[nodiscard]] auto length(V v) noexcept -> typename V::ValueType
{
    return std::sqrt(dot(v, v));
}

template<FloatVector V>
[[nodiscard]] auto distance(V a, V b) noexcept -> typename V::ValueType
{
    return length(b - a);
}

/// @brief The unit vector along `v`; the zero vector stays zero (no NaN)
template<FloatVector V>
[[nodiscard]] auto normalize(V v) noexcept -> V
{
    using T = typename V::ValueType;
    const T len = length(v);
    return len > T{ 0 } ? v / len : V{};
}

// The :scalar helpers, component-wise

template<FloatVector V>
[[nodiscard]] constexpr auto radians(V degrees) noexcept -> V
{
    return detail::map(degrees, [](auto c) { return radians(c); });
}

template<FloatVector V>
[[nodiscard]] constexpr auto degrees(V radians) noexcept -> V
{
    return detail::map(radians, [](auto c) { return degrees(c); });
}

template<VectorType V>
[[nodiscard]] constexpr auto min(V a, V b) noexcept -> V
{
    return detail::zip(a, b, [](auto x, auto y) { return min(x, y); });
}

template<VectorType V>
[[nodiscard]] constexpr auto max(V a, V b) noexcept -> V
{
    return detail::zip(a, b, [](auto x, auto y) { return max(x, y); });
}

template<VectorType V>
[[nodiscard]] constexpr auto abs(V v) noexcept -> V
{
    return detail::map(v, [](auto c) { return abs(c); });
}

template<VectorType V>
[[nodiscard]] constexpr auto clamp(V x, V low, V high) noexcept -> V
{
    return min(max(x, low), high);
}

template<VectorType V>
[[nodiscard]] constexpr auto clamp(V x, typename V::ValueType low, typename V::ValueType high) noexcept -> V
{
    return clamp(x, V{ low }, V{ high });
}

/// @brief Interpolates from `a` (t = 0) to `b` (t = 1), exactly at both ends
template<FloatVector V>
[[nodiscard]] constexpr auto lerp(V a, V b, typename V::ValueType t) noexcept -> V
{
    return detail::zip(a, b, [t](auto x, auto y) { return lerp(x, y, t); });
}

template<FloatVector V>
[[nodiscard]] constexpr auto saturate(V v) noexcept -> V
{
    return detail::map(v, [](auto c) { return saturate(c); });
}

template<FloatVector V>
[[nodiscard]] auto floor(V v) noexcept -> V
{
    return detail::map(v, [](auto c) { return floor(c); });
}

template<FloatVector V>
[[nodiscard]] auto fract(V v) noexcept -> V
{
    return detail::map(v, [](auto c) { return fract(c); });
}

template<FloatVector V>
[[nodiscard]] auto sqrt(V v) noexcept -> V
{
    return detail::map(v, [](auto c) { return sqrt(c); });
}

/// @brief Floored modulo per component (see the scalar mod)
template<FloatVector V>
[[nodiscard]] auto mod(V x, V y) noexcept -> V
{
    return detail::zip(x, y, [](auto a, auto b) { return mod(a, b); });
}

/// @brief Floored modulo of each component by one scalar
template<FloatVector V>
[[nodiscard]] auto mod(V x, typename V::ValueType y) noexcept -> V
{
    return mod(x, V{ y });
}

/// @brief Whether every component pair is nearlyEqual (see the scalar overload)
template<FloatVector V>
[[nodiscard]] constexpr auto nearlyEqual(V a, V b, typename V::ValueType tolerance = defaultTolerance<typename V::ValueType>) noexcept -> bool
{
    for (std::size_t i = 0; i < V::componentCount; ++i)
    {
        if (!nearlyEqual(a[i], b[i], tolerance))
            return false;
    }
    return true;
}
}
