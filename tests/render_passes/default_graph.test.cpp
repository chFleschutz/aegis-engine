#include <doctest/doctest.h>

import std;
import aegis.render_passes;

// The default graphs are declared and planned with no device, then compared with a checked-in plan
// (tests/render_passes/snapshots). A change to a pass's declarations, the order of the passes or a barrier
// shows up as a diff of that file. After an intended change, regenerate the files by running this test
// with AEGIS_UPDATE_SNAPSHOTS=1 and review the diff.

namespace
{
using namespace aegis::render_graph;

auto readFile(const std::filesystem::path& path) -> std::optional<std::string>
{
    std::ifstream file{ path, std::ios::binary };
    if (!file)
        return std::nullopt;
    std::string text{ std::istreambuf_iterator<char>{ file }, std::istreambuf_iterator<char>{} };
    std::erase(text, '\r'); // The checkout may have converted the line endings
    return text;
}

// MSVC marks getenv deprecated; this is a test-only read of one variable.
auto updateRequested() -> bool
{
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
    return std::getenv("AEGIS_UPDATE_SNAPSHOTS") != nullptr;
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
}

auto planOfDefaultGraph(bool gpuDriven) -> std::string
{
    GraphBuilder builder;
    addDefaultPasses(builder, { .gpuDriven = gpuDriven, .buildUi = [] {} });

    const auto plan = builder.plan();
    REQUIRE(plan.has_value());
    return aegis::render_graph::detail::describePlan(builder.declarations(), *plan);
}

void checkSnapshot(const std::string& name, const std::string& actual)
{
    const auto path = std::filesystem::path{ AEGIS_PLAN_SNAPSHOT_DIR } / name;
    if (updateRequested())
    {
        std::ofstream{ path, std::ios::binary } << actual;
        return;
    }

    const auto expected = readFile(path);
    REQUIRE_MESSAGE(expected.has_value(), "Missing snapshot ", path.string());
    CHECK_MESSAGE(actual == *expected, "The plan differs from ", path.string(), ". Actual plan:\n", actual);
}
}

TEST_SUITE("render_graph::addDefaultPasses")
{
    TEST_CASE("the CPU- and GPU-driven default graphs compile to a plan without a device")
    {
        for (const bool gpuDriven : { false, true })
        {
            CAPTURE(gpuDriven);
            GraphBuilder builder;
            addDefaultPasses(builder, { .gpuDriven = gpuDriven, .buildUi = [] {} });

            const auto plan = builder.plan();
            REQUIRE_MESSAGE(plan.has_value(), plan.error().message);
            CHECK(plan->order.size() == builder.declarations().passCount());
        }
    }

    TEST_CASE("the CPU-driven default graph matches its plan snapshot")
    {
        checkSnapshot("default_graph_cpu.txt", planOfDefaultGraph(false));
    }

    TEST_CASE("the GPU-driven default graph matches its plan snapshot")
    {
        checkSnapshot("default_graph_gpu_driven.txt", planOfDefaultGraph(true));
    }

    TEST_CASE("every pass of the default graph has a name")
    {
        GraphBuilder builder;
        addDefaultPasses(builder, { .gpuDriven = true, .buildUi = [] {} });

        for (std::uint32_t pass = 0; pass < builder.declarations().passCount(); ++pass)
            CHECK_FALSE(builder.declarations().passName(pass).empty());
    }
}
