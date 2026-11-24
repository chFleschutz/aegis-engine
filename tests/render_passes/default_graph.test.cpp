#include <doctest/doctest.h>

import std;
import aegis.render_passes;
import aegis.rhi;

// The default graphs are declared and planned with no device, then compared with a checked-in plan
// (tests/render_passes/snapshots). A change to a pass's declarations, the order of the passes or a barrier
// shows up as a diff of that file. After an intended change, regenerate the files by running this test
// with AEGIS_UPDATE_SNAPSHOTS=1 and review the diff.

namespace
{
using namespace aegis::render_graph;

const FrameSceneSource scene; // Never bound: the graphs are only planned
PipelineLibrary pipelines;    // Never asked for a pipeline: no pass creates its resources
RenderSettings settings;      // The passes only keep a reference

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

auto planOfDefaultGraph(bool gpuDriven, std::optional<DefaultPassesDesc::ShownViews> shown = std::nullopt,
    std::vector<DefaultPassesDesc::View> views = { { .name = "Main" } }) -> std::string
{
    GraphBuilder builder;
    addDefaultPasses(builder, { .scene = scene, .pipelines = pipelines, .settings = settings, .gpuDriven = gpuDriven, .ui = true, .views = std::move(views), .shownViews = shown });

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
            addDefaultPasses(builder, { .scene = scene, .pipelines = pipelines, .settings = settings, .gpuDriven = gpuDriven, .ui = true });

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

    TEST_CASE("the default graph rendering into a view matches its plan snapshot")
    {
        // The UI pass samples "Final" and comes last, with no present pass.
        checkSnapshot("default_graph_view.txt", planOfDefaultGraph(false, DefaultPassesDesc::ShownViews{ .targetFormat = aegis::rhi::Format::BGRA8_SRGB }));
    }

    TEST_CASE("the GPU-driven default graph with two views matches its plan snapshot")
    {
        // One shared instance pass; each view's scene passes with resources of their own, culling included, and the
        // barriers between them; the UI pass samples both views' "Final".
        checkSnapshot("default_graph_two_views.txt",
            planOfDefaultGraph(true, DefaultPassesDesc::ShownViews{ .targetFormat = aegis::rhi::Format::BGRA8_SRGB },
                { { .name = "Edit" }, { .name = "Play", .extent = aegis::rhi::Extent2D{ 320, 240 } } }));
    }

    TEST_CASE("the CPU-driven default graph with shadow views matches its plan snapshot")
    {
        // Each cascade's shadow view draws "MainShadow<cascade>/Depth" with the depth technique, which the main view's
        // lighting samples.
        std::vector<DefaultPassesDesc::View> views{ { .name = "Main" } };
        addShadowViews(views);
        checkSnapshot("default_graph_shadow_cpu.txt", planOfDefaultGraph(false, std::nullopt, std::move(views)));
    }

    TEST_CASE("the GPU-driven default graph with shadow views matches its plan snapshot")
    {
        // Each shadow view culls with its own camera into buffers of its own.
        std::vector<DefaultPassesDesc::View> views{ { .name = "Main" } };
        addShadowViews(views);
        checkSnapshot("default_graph_shadow_gpu_driven.txt", planOfDefaultGraph(true, std::nullopt, std::move(views)));
    }

    TEST_CASE("the spot shadow views are declared first, then the other views in their order")
    {
        std::vector<DefaultPassesDesc::View> views{ { .name = "Edit" }, { .name = "Play" } };
        addShadowViews(views);
        const auto order = viewDeclarationOrder(views);
        REQUIRE(order.size() == views.size());
        const auto others = static_cast<std::uint32_t>(2 + 2 * maxShadowCascades);
        for (std::uint32_t i = 0; i < maxShadowedSpotLights; ++i)
            CHECK(order[i] == others + i);
        for (std::uint32_t i = 0; i < others; ++i)
            CHECK(order[maxShadowedSpotLights + i] == i);
    }

    TEST_CASE("the spot shadow views draw their tiles of the atlas before the main view's lighting reads it")
    {
        for (const bool gpuDriven : { false, true })
        {
            CAPTURE(gpuDriven);
            std::vector<DefaultPassesDesc::View> views{ { .name = "Main" } };
            addShadowViews(views);
            GraphBuilder builder;
            addDefaultPasses(builder, { .scene = scene, .pipelines = pipelines, .settings = settings, .gpuDriven = gpuDriven, .ui = true, .views = std::move(views) });
            const auto plan = builder.plan();
            REQUIRE(plan.has_value());
            CHECK(builder.declarations().find("SpotShadowAtlas").has_value());
            CHECK(builder.declarations().find("SpotShadow0/Depth") == std::nullopt);

            // The position in the plan's order of each pass named 'name'.
            const auto positions = [&](std::string_view name) {
                std::vector<std::size_t> found;
                for (std::size_t i = 0; i < plan->order.size(); ++i)
                {
                    if (builder.declarations().passName(plan->order[i]) == name)
                        found.push_back(i);
                }
                return found;
            };
            const auto lighting = positions("Main/Lighting");
            REQUIRE(lighting.size() == 1);
            for (std::uint32_t index = 0; index < maxShadowedSpotLights; ++index)
            {
                CAPTURE(index);
                const auto depth = positions(std::format("SpotShadow{}/{}", index, gpuDriven ? "GPU Driven Depth" : "Depth"));
                REQUIRE(depth.size() == 1);
                CHECK(depth.front() < lighting.front());
            }
        }
    }

    TEST_CASE("only the main views reach the output passes")
    {
        std::vector<DefaultPassesDesc::View> views{ { .name = "Main" } };
        addShadowViews(views);
        GraphBuilder builder;
        addDefaultPasses(builder, { .scene = scene, .pipelines = pipelines, .settings = settings, .gpuDriven = false, .ui = true, .views = std::move(views) });
        CHECK(builder.declarations().find("MainShadow0/Final") == std::nullopt);
        CHECK(builder.declarations().find("MainShadow0/Depth").has_value());
        CHECK(builder.declarations().find("MainShadow3/Depth").has_value());
        CHECK(builder.declarations().find("Main/Final").has_value());
    }

    TEST_CASE("a GPU-driven view adds its own instance update only without the shared one")
    {
        const auto instanceUpdates = [](const GraphBuilder& builder) {
            std::vector<std::string> found;
            for (std::uint32_t pass = 0; pass < builder.declarations().passCount(); ++pass)
            {
                if (builder.declarations().passName(pass).ends_with("Instance Update"))
                    found.emplace_back(builder.declarations().passName(pass));
            }
            return found;
        };
        const DefaultPassesDesc desc{ .scene = scene, .pipelines = pipelines, .settings = settings, .gpuDriven = true };

        GraphBuilder shared;
        addSharedPasses(shared, desc);
        shared.beginScope("Main");
        addScenePasses(shared, desc);
        shared.endScope();
        CHECK(instanceUpdates(shared) == std::vector<std::string>{ "Instance Update" });

        GraphBuilder own;
        own.beginScope("Main");
        addScenePasses(own, desc);
        own.endScope();
        CHECK(instanceUpdates(own) == std::vector<std::string>{ "Main/Instance Update" });
        CHECK(own.declarations().find("Main/StaticInstances").has_value());
    }

    TEST_CASE("every pass of the default graph has a name")
    {
        GraphBuilder builder;
        addDefaultPasses(builder, { .scene = scene, .pipelines = pipelines, .settings = settings, .gpuDriven = true, .ui = true });

        for (std::uint32_t pass = 0; pass < builder.declarations().passCount(); ++pass)
            CHECK_FALSE(builder.declarations().passName(pass).empty());
    }
}
