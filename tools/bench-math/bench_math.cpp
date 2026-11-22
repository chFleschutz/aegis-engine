// aegis.math against glm on six operations, for float (gated) and double (reported):
//   Float4x4 * Float4x4, Float4x4 * Float4, inverse(Float4x4), Quat * Quat, Quat * Float3, normalize(Float3).
//
// Each operation runs over a pool of fixed-seed random inputs; each sample times both libraries back to back and
// the median of the ratios counts. The gate (AEGIS_BENCH_GATE, set per build type in CMakeLists.txt) is the largest
// accepted ratio of aegis.math's time to glm's, for float only: Release 1.10, Development 1.03 (no slower, within the run-to-run scatter), Debug none.
// The exit code is 1 when a float operation misses it, 2 when the two libraries disagree on a result.
//
// Include order: glm before any import (AGENTS.md); the macros match aegis.math's own glm include.
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

import std;
import aegis.math;

namespace math = aegis::math;

#ifndef AEGIS_BENCH_GATE
#define AEGIS_BENCH_GATE 0.0
#endif

namespace
{
constexpr std::size_t poolSize = 128; // small enough that the inputs and results stay in the L1 cache

#if defined(__clang__) || defined(__GNUC__)
/// @brief Hides a pointer's origin from the optimiser and makes it assume all memory changed
template<typename T>
auto opaque(T* p) -> T*
{
    asm volatile("" : "+r"(p) : : "memory");
    return p;
}

/// @brief Makes the optimiser assume the results in memory are read
void keep(const void* p)
{
    asm volatile("" : : "r"(p) : "memory");
}
#else
static_assert(false, "aegis-bench-math needs the GNU asm barrier of Clang or GCC");
#endif

using Clock = std::chrono::steady_clock;

struct Options
{
    double gate = AEGIS_BENCH_GATE;
    int samples = 41;
    double sampleMs = 8.0;
};

struct Result
{
    double ownNs; // nanoseconds per operation
    double glmNs;
    double ratio; // aegis.math's time over glm's
};

auto median(std::vector<double>& values) -> double
{
    std::ranges::sort(values);
    return values[values.size() / 2];
}

/// @brief Nanoseconds per call of two batches, each of which makes `poolSize` calls
///
/// A sample times both back to back, in alternating order, and its ratio is taken inside the pair; the result is the
/// median over the samples. The clock speed of a laptop or a busy desktop drifts by more than the gate allows, but it
/// drifts alike for the two halves of a pair.
template<typename OwnBatch, typename GlmBatch>
auto measure(OwnBatch&& own, GlmBatch&& glmBatch, const Options& options) -> Result
{
    auto timed = [](auto& batch, std::size_t reps)
    {
        auto start = Clock::now();
        for (std::size_t i = 0; i < reps; ++i)
            batch();
        return std::chrono::duration<double, std::nano>(Clock::now() - start).count();
    };

    // Calibrate the repetitions on the slower side, so that a timing lasts about sampleMs.
    std::size_t reps = 1;
    while (std::max(timed(own, reps), timed(glmBatch, reps)) < options.sampleMs * 1e6)
        reps *= 2;

    std::vector<double> ownNs;
    std::vector<double> glmNs;
    std::vector<double> ratios;
    auto perOp = static_cast<double>(reps * poolSize);
    for (int s = 0; s < options.samples; ++s)
    {
        double ownTime = 0.0;
        double glmTime = 0.0;
        if (s % 2 == 0)
        {
            ownTime = timed(own, reps);
            glmTime = timed(glmBatch, reps);
        }
        else
        {
            glmTime = timed(glmBatch, reps);
            ownTime = timed(own, reps);
        }
        ownNs.push_back(ownTime / perOp);
        glmNs.push_back(glmTime / perOp);
        ratios.push_back(ownTime / glmTime);
    }
    return { median(ownNs), median(glmNs), median(ratios) };
}

/// @brief A value in [-1, 1] on a 0.001 grid from a random bit generator: identical on every standard library
template<typename T, typename Rng>
auto randomScalar(Rng& rng) -> T
{
    return static_cast<T>(static_cast<double>(rng() % 2001u) / 1000.0 - 1.0);
}

/// @brief The scalar type's glm and aegis.math types for the six operations
template<typename T>
struct Types
{
    using GlmMat = glm::mat<4, 4, T>;
    using GlmVec4 = glm::vec<4, T>;
    using GlmVec3 = glm::vec<3, T>;
    using GlmQuat = glm::qua<T>;
    using OwnMat = math::Matrix<T, 4, 4>;
    using OwnVec4 = math::Vector<T, 4>;
    using OwnVec3 = math::Vector<T, 3>;
    using OwnQuat = math::Quaternion<T>;
};

/// @brief Largest difference between the two libraries' results, relative to the result's size, over all pairs
class Agreement
{
public:
    template<typename Own, typename Glm>
    void compare(const std::vector<Own>& own, const std::vector<Glm>& glmResults)
    {
        for (std::size_t i = 0; i < own.size(); ++i)
            visit(own[i], glmResults[i]);
    }

    [[nodiscard]] auto worst() const -> double { return m_worst; }

private:
    template<typename T>
    void value(T own, T glm)
    {
        auto scale = std::max<double>(1.0, std::abs(static_cast<double>(glm)));
        m_worst = std::max(m_worst, std::abs(static_cast<double>(own) - static_cast<double>(glm)) / scale);
    }

    template<typename T>
    void visit(const math::Matrix<T, 4, 4>& own, const glm::mat<4, 4, T>& glm)
    {
        for (std::size_t r = 0; r < 4; ++r)
        {
            for (std::size_t c = 0; c < 4; ++c)
                value(own[r, c], glm[static_cast<glm::length_t>(c)][static_cast<glm::length_t>(r)]);
        }
    }

    template<typename T, std::size_t N>
    void visit(const math::Vector<T, N>& own, const glm::vec<static_cast<glm::length_t>(N), T>& glm)
    {
        for (std::size_t i = 0; i < N; ++i)
            value(own[i], glm[static_cast<glm::length_t>(i)]);
    }

    template<typename T>
    void visit(const math::Quaternion<T>& own, const glm::qua<T>& glm)
    {
        value(own.x, glm.x);
        value(own.y, glm.y);
        value(own.z, glm.z);
        value(own.w, glm.w);
    }

    double m_worst = 0.0;
};

/// @brief One operation's inputs in both libraries (the same values, so the results are comparable) and its results
template<typename GlmA, typename OwnA, typename GlmB, typename OwnB, typename GlmOut, typename OwnOut>
struct Pool
{
    template<typename GenA, typename ConvA, typename GenB, typename ConvB>
    Pool(GenA genA, ConvA convA, GenB genB, ConvB convB, const OwnOut& ownOutInit)
        : glmOut(poolSize)
        , ownOut(poolSize, ownOutInit)
    {
        for (std::size_t i = 0; i < poolSize; ++i)
        {
            glmA.push_back(genA());
            glmB.push_back(genB());
            ownA.push_back(convA(glmA.back()));
            ownB.push_back(convB(glmB.back()));
        }
    }

    std::vector<GlmA> glmA;
    std::vector<GlmB> glmB;
    std::vector<OwnA> ownA;
    std::vector<OwnB> ownB;
    std::vector<GlmOut> glmOut;
    std::vector<OwnOut> ownOut;
};

template<typename T>
class Suite
{
public:
    Suite(const Options& options, bool gated)
        : m_options{ options }
        , m_gated{ gated }
    {
        std::print("\n{}\n{:<26}{:>12}{:>12}{:>9}\n", gated ? "float" : "double (reported)", "operation", "aegis ns", "glm ns", "ratio");
    }

    [[nodiscard]] auto run() -> int
    {
        using Ty = Types<T>;
        std::mt19937 rng{ 20261121u };

        // Matrices are strictly diagonally dominant, so they are never singular and inverse stays well conditioned.
        auto glmMatrix = [&]
        {
            typename Ty::GlmMat m{ 1.0f };
            for (glm::length_t c = 0; c < 4; ++c)
            {
                for (glm::length_t r = 0; r < 4; ++r)
                    m[c][r] = randomScalar<T>(rng) + (r == c ? T{ 4 } : T{ 0 });
            }
            return m;
        };
        auto ownMatrix = [](const typename Ty::GlmMat& g)
        {
            auto m = Ty::OwnMat::zero();
            for (std::size_t r = 0; r < 4; ++r)
            {
                for (std::size_t c = 0; c < 4; ++c)
                    m[r, c] = g[static_cast<glm::length_t>(c)][static_cast<glm::length_t>(r)];
            }
            return m;
        };
        auto glmVec4 = [&] { return typename Ty::GlmVec4{ randomScalar<T>(rng), randomScalar<T>(rng), randomScalar<T>(rng), randomScalar<T>(rng) }; };
        auto glmVec3 = [&] { return typename Ty::GlmVec3{ randomScalar<T>(rng), randomScalar<T>(rng) + T{ 2 }, randomScalar<T>(rng) }; }; // never zero
        auto glmQuat = [&]
        {
            typename Ty::GlmQuat q{ randomScalar<T>(rng) + T{ 2 }, randomScalar<T>(rng), randomScalar<T>(rng), randomScalar<T>(rng) };
            return glm::normalize(q);
        };
        auto ownVec4 = [](const typename Ty::GlmVec4& g) { return typename Ty::OwnVec4{ g.x, g.y, g.z, g.w }; };
        auto ownVec3 = [](const typename Ty::GlmVec3& g) { return typename Ty::OwnVec3{ g.x, g.y, g.z }; };
        auto ownQuat = [](const typename Ty::GlmQuat& g) { return Ty::OwnQuat::fromXyzw(g.x, g.y, g.z, g.w); };

        int failed = 0;
        auto mul = [](const auto& a, const auto& b) { return a * b; };
        const auto identity = Ty::OwnMat::identity();
        const auto identityQuat = Ty::OwnQuat::identity();

        {
            Pool<typename Ty::GlmMat, typename Ty::OwnMat, typename Ty::GlmMat, typename Ty::OwnMat, typename Ty::GlmMat, typename Ty::OwnMat> p{ glmMatrix, ownMatrix, glmMatrix, ownMatrix, identity };
            failed += report("mat4 * mat4", p, mul, mul);
        }
        {
            Pool<typename Ty::GlmMat, typename Ty::OwnMat, typename Ty::GlmVec4, typename Ty::OwnVec4, typename Ty::GlmVec4, typename Ty::OwnVec4> p{ glmMatrix, ownMatrix, glmVec4, ownVec4, typename Ty::OwnVec4{} };
            failed += report("mat4 * vec4", p, mul, mul);
        }
        {
            Pool<typename Ty::GlmMat, typename Ty::OwnMat, typename Ty::GlmMat, typename Ty::OwnMat, typename Ty::GlmMat, typename Ty::OwnMat> p{ glmMatrix, ownMatrix, glmMatrix, ownMatrix, identity };
            failed += reportUnary("inverse(mat4)", p, [](const auto& a) { return math::inverse(a); }, [](const auto& a) { return glm::inverse(a); });
        }
        {
            Pool<typename Ty::GlmQuat, typename Ty::OwnQuat, typename Ty::GlmQuat, typename Ty::OwnQuat, typename Ty::GlmQuat, typename Ty::OwnQuat> p{ glmQuat, ownQuat, glmQuat, ownQuat, identityQuat };
            failed += report("quat * quat", p, mul, mul);
        }
        {
            Pool<typename Ty::GlmQuat, typename Ty::OwnQuat, typename Ty::GlmVec3, typename Ty::OwnVec3, typename Ty::GlmVec3, typename Ty::OwnVec3> p{ glmQuat, ownQuat, glmVec3, ownVec3, typename Ty::OwnVec3{} };
            failed += report("quat * vec3", p, mul, mul);
        }
        {
            Pool<typename Ty::GlmVec3, typename Ty::OwnVec3, typename Ty::GlmVec3, typename Ty::OwnVec3, typename Ty::GlmVec3, typename Ty::OwnVec3> p{ glmVec3, ownVec3, glmVec3, ownVec3, typename Ty::OwnVec3{} };
            failed += reportUnary("normalize(vec3)", p, [](const auto& a) { return math::normalize(a); }, [](const auto& a) { return glm::normalize(a); });
        }
        return failed;
    }

private:
    template<typename P, typename OwnOp, typename GlmOp>
    auto report(const char* name, P& p, OwnOp ownOp, GlmOp glmOp) -> int
    {
        auto ownBatch = [&]
        {
            const auto* a = opaque(p.ownA.data());
            const auto* b = opaque(p.ownB.data());
            auto* out = p.ownOut.data();
            for (std::size_t i = 0; i < poolSize; ++i)
                out[i] = ownOp(a[i], b[i]);
            keep(out);
        };
        auto glmBatch = [&]
        {
            const auto* a = opaque(p.glmA.data());
            const auto* b = opaque(p.glmB.data());
            auto* out = p.glmOut.data();
            for (std::size_t i = 0; i < poolSize; ++i)
                out[i] = glmOp(a[i], b[i]);
            keep(out);
        };
        return finish(name, p, ownBatch, glmBatch);
    }

    template<typename P, typename OwnOp, typename GlmOp>
    auto reportUnary(const char* name, P& p, OwnOp ownOp, GlmOp glmOp) -> int
    {
        auto ownBatch = [&]
        {
            const auto* a = opaque(p.ownA.data());
            auto* out = p.ownOut.data();
            for (std::size_t i = 0; i < poolSize; ++i)
                out[i] = ownOp(a[i]);
            keep(out);
        };
        auto glmBatch = [&]
        {
            const auto* a = opaque(p.glmA.data());
            auto* out = p.glmOut.data();
            for (std::size_t i = 0; i < poolSize; ++i)
                out[i] = glmOp(a[i]);
            keep(out);
        };
        return finish(name, p, ownBatch, glmBatch);
    }

    /// @return 1 when the operation is gated and misses it, 100 when the libraries disagree
    template<typename P, typename OwnBatch, typename GlmBatch>
    auto finish(const char* name, P& p, OwnBatch& ownBatch, GlmBatch& glmBatch) -> int
    {
        // One run of each first, so that a wrong operation is caught before it is timed.
        ownBatch();
        glmBatch();
        Agreement agreement;
        agreement.compare(p.ownOut, p.glmOut);
        auto tolerance = std::is_same_v<T, float> ? 1e-3 : 1e-9;
        if (agreement.worst() > tolerance)
        {
            std::print("{:<26}the libraries disagree by {:.3g}\n", name, agreement.worst());
            return 100;
        }

        auto result = measure(ownBatch, glmBatch, m_options);
        auto miss = m_gated && m_options.gate > 0.0 && result.ratio > m_options.gate;
        std::print("{:<26}{:>12.3f}{:>12.3f}{:>9.2f}{}\n", name, result.ownNs, result.glmNs, result.ratio, miss ? "  over the gate" : "");
        return miss ? 1 : 0;
    }

    Options m_options;
    bool m_gated;
};

auto parse(int argc, char** argv) -> std::optional<Options>
{
    Options options;
    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        const char* value = nullptr;
        if (arg == "--gate" || arg == "--samples" || arg == "--sample-ms")
            value = next();
        if (!value)
        {
            std::print(stderr, "usage: aegis-bench-math [--gate <max ratio, 0 = none>] [--samples <n>] [--sample-ms <ms>]\n");
            return std::nullopt;
        }
        std::string_view text = value;
        auto parsed = arg == "--samples" ? std::from_chars(text.data(), text.data() + text.size(), options.samples)
                                         : std::from_chars(text.data(), text.data() + text.size(), arg == "--gate" ? options.gate : options.sampleMs);
        if (parsed.ec != std::errc{})
        {
            std::print(stderr, "{}: not a number: {}\n", arg, text);
            return std::nullopt;
        }
    }
    return options;
}
}

auto main(int argc, char** argv) -> int
{
    auto options = parse(argc, argv);
    if (!options)
        return 64;

    std::print("aegis.math vs glm, {} samples of {} ms, gate {}\n", options->samples, options->sampleMs,
        options->gate > 0.0 ? std::format("ratio <= {:.2f}", options->gate) : "none");

    auto floatFailures = Suite<float>{ *options, true }.run();
    // double is for information: the gate does not apply, but a disagreement still fails.
    auto doubleFailures = Suite<double>{ *options, false }.run();

    if (floatFailures >= 100 || doubleFailures >= 100)
        return 2;
    return floatFailures > 0 ? 1 : 0;
}
