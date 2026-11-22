module;

#include <cassert>

export module aegis.math:matrix;

import std;
import :scalar;
import :vector;

export namespace aegis::math
{
/// @brief An R x C matrix of T (R rows, C columns), stored row-major as R * C consecutive T
/// @note Vectors are columns, so `M * v` transforms `v` and `projection * view * model` composes right to left.
/// An element is `m[row, column]`; there is no single-index `[]`, so glm's `m[column][row]` does not compile.
/// There is no public default constructor: the state is always named (`zero()`, `identity()`, `fromRows`).
template<Scalar T, std::size_t R, std::size_t C>
    requires Dimension<R> && Dimension<C>
struct Matrix
{
    using ValueType = T;
    static constexpr std::size_t rowCount = R;
    static constexpr std::size_t columnCount = C;

    [[nodiscard]] static constexpr auto zero() noexcept -> Matrix
    {
        return Matrix{};
    }

    [[nodiscard]] static constexpr auto identity() noexcept -> Matrix
        requires(R == C)
    {
        Matrix result{};
        for (std::size_t i = 0; i < R; ++i)
            result[i, i] = T{ 1 };
        return result;
    }

    template<std::same_as<Vector<T, C>>... Rows>
        requires(sizeof...(Rows) == R)
    [[nodiscard]] static constexpr auto fromRows(Rows... rows) noexcept -> Matrix
    {
        Matrix result{};
        std::size_t r = 0;
        (result.setRow(r++, rows), ...);
        return result;
    }

    /// @brief Builds the matrix from its columns, the layout of glTF, ImGuizmo and glm
    template<std::same_as<Vector<T, R>>... Columns>
        requires(sizeof...(Columns) == C)
    [[nodiscard]] static constexpr auto fromColumns(Columns... columns) noexcept -> Matrix
    {
        Matrix result{};
        std::size_t c = 0;
        (result.setColumn(c++, columns), ...);
        return result;
    }

    /// @brief Converts the shape and scalar type: keeps the overlapping block, fills the rest from identity
    /// @note `Float3x4{ m4 }` keeps the top three rows, `Float4x4{ m34 }` appends `0 0 0 1`, `Float3x3{ m4 }` keeps
    /// the upper-left block, and `Float4x4{ someDouble4x4 }` converts each element.
    template<Scalar U, std::size_t R2, std::size_t C2>
    explicit constexpr Matrix(const Matrix<U, R2, C2>& m) noexcept
    {
        for (std::size_t r = 0; r < R; ++r)
        {
            for (std::size_t c = 0; c < C; ++c)
            {
                if (r < R2 && c < C2)
                    (*this)[r, c] = static_cast<T>(m[r, c]);
                else
                    (*this)[r, c] = r == c ? T{ 1 } : T{ 0 };
            }
        }
    }

    [[nodiscard]] constexpr auto operator[](std::size_t row, std::size_t column) noexcept -> T&
    {
        assert(row < R && column < C);
        return m_elements[row * C + column];
    }

    [[nodiscard]] constexpr auto operator[](std::size_t row, std::size_t column) const noexcept -> const T&
    {
        assert(row < R && column < C);
        return m_elements[row * C + column];
    }

    [[nodiscard]] constexpr auto row(std::size_t i) const noexcept -> Vector<T, C>
    {
        return detail::generate<T, C>([&](std::size_t c) { return (*this)[i, c]; });
    }

    [[nodiscard]] constexpr auto column(std::size_t i) const noexcept -> Vector<T, R>
    {
        return detail::generate<T, R>([&](std::size_t r) { return (*this)[r, i]; });
    }

    constexpr void setRow(std::size_t i, Vector<T, C> v) noexcept
    {
        for (std::size_t c = 0; c < C; ++c)
            (*this)[i, c] = v[c];
    }

    constexpr void setColumn(std::size_t i, Vector<T, R> v) noexcept
    {
        for (std::size_t r = 0; r < R; ++r)
            (*this)[r, i] = v[r];
    }

    friend constexpr auto operator==(const Matrix&, const Matrix&) noexcept -> bool = default;

private:
    constexpr Matrix() noexcept = default; // for the factories only

    T m_elements[R * C]{}; // row-major: row * C + column
};

namespace detail
{
template<typename M>
struct IsMatrix : std::false_type
{
};

template<Scalar T, std::size_t R, std::size_t C>
    requires Dimension<R> && Dimension<C>
struct IsMatrix<Matrix<T, R, C>> : std::true_type
{
};
}

/// @brief Any Matrix<T, R, C>
template<typename M>
concept MatrixType = detail::IsMatrix<M>::value;

// Products: the linear-algebra product (glm's meaning). Slang's `*` on matrices is per element, so code
// mirrored from a shader writes `mul`. There is no row-vector product `v * M`.

template<Scalar T, std::size_t R, std::size_t K, std::size_t C>
[[nodiscard]] constexpr auto operator*(const Matrix<T, R, K>& a, const Matrix<T, K, C>& b) noexcept -> Matrix<T, R, C>
{
    auto result = Matrix<T, R, C>::zero();
    for (std::size_t r = 0; r < R; ++r)
    {
        for (std::size_t c = 0; c < C; ++c)
        {
            for (std::size_t k = 0; k < K; ++k)
                result[r, c] += a[r, k] * b[k, c];
        }
    }
    return result;
}

template<Scalar T, std::size_t R, std::size_t C>
[[nodiscard]] constexpr auto operator*(const Matrix<T, R, C>& m, Vector<T, C> v) noexcept -> Vector<T, R>
{
    return detail::generate<T, R>([&](std::size_t r) { return dot(m.row(r), v); });
}

// The 4x4 shapes, which the engine multiplies every frame and per instance, are written out: in the Development
// build type (-O1) the generic loops above measured 5x (matrix) and 11x (vector) glm's time in tools/bench_math.
// Same values as the generic kernels; the differential tests in tests/math pick these overloads for Float4x4.

template<Scalar T>
[[nodiscard]] constexpr auto operator*(const Matrix<T, 4, 4>& a, const Matrix<T, 4, 4>& b) noexcept -> Matrix<T, 4, 4>
{
    auto result = Matrix<T, 4, 4>::zero();
    for (std::size_t r = 0; r < 4; ++r)
    {
        const T a0 = a[r, 0];
        const T a1 = a[r, 1];
        const T a2 = a[r, 2];
        const T a3 = a[r, 3];
        result[r, 0] = a0 * b[0, 0] + a1 * b[1, 0] + a2 * b[2, 0] + a3 * b[3, 0];
        result[r, 1] = a0 * b[0, 1] + a1 * b[1, 1] + a2 * b[2, 1] + a3 * b[3, 1];
        result[r, 2] = a0 * b[0, 2] + a1 * b[1, 2] + a2 * b[2, 2] + a3 * b[3, 2];
        result[r, 3] = a0 * b[0, 3] + a1 * b[1, 3] + a2 * b[2, 3] + a3 * b[3, 3];
    }
    return result;
}

template<Scalar T>
[[nodiscard]] constexpr auto operator*(const Matrix<T, 4, 4>& m, Vector<T, 4> v) noexcept -> Vector<T, 4>
{
    return {
        m[0, 0] * v.x + m[0, 1] * v.y + m[0, 2] * v.z + m[0, 3] * v.w,
        m[1, 0] * v.x + m[1, 1] * v.y + m[1, 2] * v.z + m[1, 3] * v.w,
        m[2, 0] * v.x + m[2, 1] * v.y + m[2, 2] * v.z + m[2, 3] * v.w,
        m[3, 0] * v.x + m[3, 1] * v.y + m[3, 2] * v.z + m[3, 3] * v.w,
    };
}

/// @brief The matrix product under its Slang name, the same as `a * b`
template<Scalar T, std::size_t R, std::size_t K, std::size_t C>
[[nodiscard]] constexpr auto mul(const Matrix<T, R, K>& a, const Matrix<T, K, C>& b) noexcept -> Matrix<T, R, C>
{
    return a * b;
}

/// @brief The matrix-vector product under its Slang name, the same as `m * v`
template<Scalar T, std::size_t R, std::size_t C>
[[nodiscard]] constexpr auto mul(const Matrix<T, R, C>& m, Vector<T, C> v) noexcept -> Vector<T, R>
{
    return m * v;
}

// Element-wise sums and scaling by a scalar

template<Scalar T, std::size_t R, std::size_t C>
[[nodiscard]] constexpr auto operator+(const Matrix<T, R, C>& a, const Matrix<T, R, C>& b) noexcept -> Matrix<T, R, C>
{
    auto result = a;
    for (std::size_t r = 0; r < R; ++r)
    {
        for (std::size_t c = 0; c < C; ++c)
            result[r, c] += b[r, c];
    }
    return result;
}

template<Scalar T, std::size_t R, std::size_t C>
[[nodiscard]] constexpr auto operator-(const Matrix<T, R, C>& a, const Matrix<T, R, C>& b) noexcept -> Matrix<T, R, C>
{
    auto result = a;
    for (std::size_t r = 0; r < R; ++r)
    {
        for (std::size_t c = 0; c < C; ++c)
            result[r, c] -= b[r, c];
    }
    return result;
}

template<Scalar T, std::size_t R, std::size_t C>
[[nodiscard]] constexpr auto operator*(const Matrix<T, R, C>& m, std::type_identity_t<T> s) noexcept -> Matrix<T, R, C>
{
    auto result = m;
    for (std::size_t r = 0; r < R; ++r)
    {
        for (std::size_t c = 0; c < C; ++c)
            result[r, c] *= s;
    }
    return result;
}

template<Scalar T, std::size_t R, std::size_t C>
[[nodiscard]] constexpr auto operator*(std::type_identity_t<T> s, const Matrix<T, R, C>& m) noexcept -> Matrix<T, R, C>
{
    return m * s;
}

template<Scalar T, std::size_t R, std::size_t C>
constexpr auto operator+=(Matrix<T, R, C>& a, const Matrix<T, R, C>& b) noexcept -> Matrix<T, R, C>&
{
    return a = a + b;
}

template<Scalar T, std::size_t R, std::size_t C>
constexpr auto operator-=(Matrix<T, R, C>& a, const Matrix<T, R, C>& b) noexcept -> Matrix<T, R, C>&
{
    return a = a - b;
}

template<Scalar T, std::size_t R, std::size_t C>
constexpr auto operator*=(Matrix<T, R, C>& m, std::type_identity_t<T> s) noexcept -> Matrix<T, R, C>&
{
    return m = m * s;
}

// Linear algebra

template<Scalar T, std::size_t R, std::size_t C>
[[nodiscard]] constexpr auto transpose(const Matrix<T, R, C>& m) noexcept -> Matrix<T, C, R>
{
    auto result = Matrix<T, C, R>::zero();
    for (std::size_t r = 0; r < R; ++r)
    {
        for (std::size_t c = 0; c < C; ++c)
            result[c, r] = m[r, c];
    }
    return result;
}

template<std::floating_point T, std::size_t N>
[[nodiscard]] constexpr auto determinant(const Matrix<T, N, N>& m) noexcept -> T
{
    if constexpr (N == 2)
    {
        return m[0, 0] * m[1, 1] - m[0, 1] * m[1, 0];
    }
    else if constexpr (N == 3)
    {
        return m[0, 0] * (m[1, 1] * m[2, 2] - m[1, 2] * m[2, 1]) -
               m[0, 1] * (m[1, 0] * m[2, 2] - m[1, 2] * m[2, 0]) +
               m[0, 2] * (m[1, 0] * m[2, 1] - m[1, 1] * m[2, 0]);
    }
    else
    {
        // Laplace expansion along the top two rows: 2x2 minors of rows 0-1 against those of rows 2-3
        const T s0 = m[0, 0] * m[1, 1] - m[1, 0] * m[0, 1];
        const T s1 = m[0, 0] * m[1, 2] - m[1, 0] * m[0, 2];
        const T s2 = m[0, 0] * m[1, 3] - m[1, 0] * m[0, 3];
        const T s3 = m[0, 1] * m[1, 2] - m[1, 1] * m[0, 2];
        const T s4 = m[0, 1] * m[1, 3] - m[1, 1] * m[0, 3];
        const T s5 = m[0, 2] * m[1, 3] - m[1, 2] * m[0, 3];
        const T c0 = m[2, 0] * m[3, 1] - m[3, 0] * m[2, 1];
        const T c1 = m[2, 0] * m[3, 2] - m[3, 0] * m[2, 2];
        const T c2 = m[2, 0] * m[3, 3] - m[3, 0] * m[2, 3];
        const T c3 = m[2, 1] * m[3, 2] - m[3, 1] * m[2, 2];
        const T c4 = m[2, 1] * m[3, 3] - m[3, 1] * m[2, 3];
        const T c5 = m[2, 2] * m[3, 3] - m[3, 2] * m[2, 3];
        return s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;
    }
}

/// @brief The inverse of a square matrix
/// @note A singular matrix is the caller's precondition (as in glm and Slang): the result is then inf or NaN.
template<std::floating_point T, std::size_t N>
[[nodiscard]] constexpr auto inverse(const Matrix<T, N, N>& m) noexcept -> Matrix<T, N, N>
{
    auto result = Matrix<T, N, N>::zero();
    if constexpr (N == 2)
    {
        const T invDet = T{ 1 } / determinant(m);
        result[0, 0] = m[1, 1] * invDet;
        result[0, 1] = -m[0, 1] * invDet;
        result[1, 0] = -m[1, 0] * invDet;
        result[1, 1] = m[0, 0] * invDet;
    }
    else if constexpr (N == 3)
    {
        const T invDet = T{ 1 } / determinant(m);
        result[0, 0] = (m[1, 1] * m[2, 2] - m[1, 2] * m[2, 1]) * invDet;
        result[0, 1] = (m[0, 2] * m[2, 1] - m[0, 1] * m[2, 2]) * invDet;
        result[0, 2] = (m[0, 1] * m[1, 2] - m[0, 2] * m[1, 1]) * invDet;
        result[1, 0] = (m[1, 2] * m[2, 0] - m[1, 0] * m[2, 2]) * invDet;
        result[1, 1] = (m[0, 0] * m[2, 2] - m[0, 2] * m[2, 0]) * invDet;
        result[1, 2] = (m[0, 2] * m[1, 0] - m[0, 0] * m[1, 2]) * invDet;
        result[2, 0] = (m[1, 0] * m[2, 1] - m[1, 1] * m[2, 0]) * invDet;
        result[2, 1] = (m[0, 1] * m[2, 0] - m[0, 0] * m[2, 1]) * invDet;
        result[2, 2] = (m[0, 0] * m[1, 1] - m[0, 1] * m[1, 0]) * invDet;
    }
    else
    {
        // The adjugate from the same 2x2 minors as determinant()
        const T s0 = m[0, 0] * m[1, 1] - m[1, 0] * m[0, 1];
        const T s1 = m[0, 0] * m[1, 2] - m[1, 0] * m[0, 2];
        const T s2 = m[0, 0] * m[1, 3] - m[1, 0] * m[0, 3];
        const T s3 = m[0, 1] * m[1, 2] - m[1, 1] * m[0, 2];
        const T s4 = m[0, 1] * m[1, 3] - m[1, 1] * m[0, 3];
        const T s5 = m[0, 2] * m[1, 3] - m[1, 2] * m[0, 3];
        const T c0 = m[2, 0] * m[3, 1] - m[3, 0] * m[2, 1];
        const T c1 = m[2, 0] * m[3, 2] - m[3, 0] * m[2, 2];
        const T c2 = m[2, 0] * m[3, 3] - m[3, 0] * m[2, 3];
        const T c3 = m[2, 1] * m[3, 2] - m[3, 1] * m[2, 2];
        const T c4 = m[2, 1] * m[3, 3] - m[3, 1] * m[2, 3];
        const T c5 = m[2, 2] * m[3, 3] - m[3, 2] * m[2, 3];
        const T invDet = T{ 1 } / (s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0);

        result[0, 0] = (m[1, 1] * c5 - m[1, 2] * c4 + m[1, 3] * c3) * invDet;
        result[0, 1] = (-m[0, 1] * c5 + m[0, 2] * c4 - m[0, 3] * c3) * invDet;
        result[0, 2] = (m[3, 1] * s5 - m[3, 2] * s4 + m[3, 3] * s3) * invDet;
        result[0, 3] = (-m[2, 1] * s5 + m[2, 2] * s4 - m[2, 3] * s3) * invDet;
        result[1, 0] = (-m[1, 0] * c5 + m[1, 2] * c2 - m[1, 3] * c1) * invDet;
        result[1, 1] = (m[0, 0] * c5 - m[0, 2] * c2 + m[0, 3] * c1) * invDet;
        result[1, 2] = (-m[3, 0] * s5 + m[3, 2] * s2 - m[3, 3] * s1) * invDet;
        result[1, 3] = (m[2, 0] * s5 - m[2, 2] * s2 + m[2, 3] * s1) * invDet;
        result[2, 0] = (m[1, 0] * c4 - m[1, 1] * c2 + m[1, 3] * c0) * invDet;
        result[2, 1] = (-m[0, 0] * c4 + m[0, 1] * c2 - m[0, 3] * c0) * invDet;
        result[2, 2] = (m[3, 0] * s4 - m[3, 1] * s2 + m[3, 3] * s0) * invDet;
        result[2, 3] = (-m[2, 0] * s4 + m[2, 1] * s2 - m[2, 3] * s0) * invDet;
        result[3, 0] = (-m[1, 0] * c3 + m[1, 1] * c1 - m[1, 2] * c0) * invDet;
        result[3, 1] = (m[0, 0] * c3 - m[0, 1] * c1 + m[0, 2] * c0) * invDet;
        result[3, 2] = (-m[3, 0] * s3 + m[3, 1] * s1 - m[3, 2] * s0) * invDet;
        result[3, 3] = (m[2, 0] * s3 - m[2, 1] * s1 + m[2, 2] * s0) * invDet;
    }
    return result;
}

/// @brief The inverse of an affine matrix (last row `0 ... 0 1`): inverts the linear block, then the translation
/// @note Cheaper than inverse() and exact in the last row; the last row of the input is assumed, not read.
template<std::floating_point T, std::size_t N>
    requires(N == 3 || N == 4)
[[nodiscard]] constexpr auto affineInverse(const Matrix<T, N, N>& m) noexcept -> Matrix<T, N, N>
{
    const auto linearInverse = inverse(Matrix<T, N - 1, N - 1>{ m });
    const auto translation = Vector<T, N - 1>{ m.column(N - 1) };
    Matrix<T, N, N> result{ linearInverse };
    result.setColumn(N - 1, Vector<T, N>{ -(linearInverse * translation), T{ 1 } });
    return result;
}

/// @brief Whether every element pair is nearlyEqual (see the scalar overload)
template<std::floating_point T, std::size_t R, std::size_t C>
[[nodiscard]] constexpr auto nearlyEqual(const Matrix<T, R, C>& a, const Matrix<T, R, C>& b, T tolerance = defaultTolerance<T>) noexcept -> bool
{
    for (std::size_t r = 0; r < R; ++r)
    {
        for (std::size_t c = 0; c < C; ++c)
        {
            if (!nearlyEqual(a[r, c], b[r, c], tolerance))
                return false;
        }
    }
    return true;
}
}
