#include <doctest/doctest.h>

import std;
import aegis.engine;
import aegis.render_graph;
import aegis.render_passes;
import aegis.rhi;

// The engine's frame graph as buildFrameGraph declares it, with a game's and a shell's passes, planned with no
// device.

namespace
{
using namespace aegis::render_graph;

// Draws onto "Final", as an overlay would.
class OverlayPass final : public RenderPass
{
public:
    OverlayPass(ResourceDeclarations& declarations, std::string_view name) :
        m_name{ name },
        m_final{ declarations.addReference(names::finalImage, Usage::StorageWrite) }
    {
    }

    [[nodiscard]] auto name() const -> std::string_view override { return m_name; }
    auto execute(ResourcePool& /*pool*/, const FrameInfo& /*frameInfo*/) -> void override {}

private:
    std::string_view m_name;
    ResourceId m_final;
};

// Creates "Final" itself, in place of the engine's scene passes.
class FinalPass final : public RenderPass
{
public:
    explicit FinalPass(ResourceDeclarations& declarations) :
        m_final{ declarations.addTexture(names::finalImage, Usage::StorageWrite, { .format = aegis::rhi::Format::RGBA16_SFLOAT, .usage = aegis::rhi::ImageUsage::TransferSrc }) }
    {
    }

    [[nodiscard]] auto name() const -> std::string_view override { return "Game Final"; }
    auto execute(ResourcePool& /*pool*/, const FrameInfo& /*frameInfo*/) -> void override {}

private:
    ResourceId m_final;
};

class OverlayShell final : public aegis::Shell
{
public:
    void onFrame(aegis::FrameContext& /*frame*/) override {}
    void buildGraph(GraphBuilder& graph, const DefaultPassesDesc& /*desc*/) override { graph.add<OverlayPass>("Shell Overlay"); }
};

// The names of the planned passes, in the order they run.
auto plannedNames(const GraphBuilder& builder) -> std::vector<std::string_view>
{
    const auto plan = builder.plan();
    REQUIRE_MESSAGE(plan.has_value(), plan.error().message);
    std::vector<std::string_view> names;
    for (const auto pass : plan->order)
        names.push_back(builder.declarations().passName(pass));
    return names;
}

auto position(const std::vector<std::string_view>& names, std::string_view name) -> std::ptrdiff_t
{
    const auto found = std::ranges::find(names, name);
    REQUIRE_MESSAGE(found != names.end(), "No pass ", name);
    return found - names.begin();
}
}

TEST_SUITE("engine::buildFrameGraph")
{
    TEST_CASE("without a game hook and with the game shell the frame graph is the default graph")
    {
        const std::optional<DefaultPassesDesc::View> view{ DefaultPassesDesc::View{ .targetFormat = aegis::rhi::Format::BGRA8_SRGB } };
        for (const bool gpuDriven : { false, true })
        {
            for (const bool withView : { false, true })
            {
                CAPTURE(gpuDriven);
                CAPTURE(withView);
                const DefaultPassesDesc desc{ .gpuDriven = gpuDriven, .ui = true, .view = withView ? view : std::nullopt };
                GraphBuilder engine;
                aegis::GameShell shell;
                buildFrameGraph(engine, desc, {}, shell);
                GraphBuilder defaults;
                addDefaultPasses(defaults, desc);

                const auto enginePlan = engine.plan();
                const auto defaultPlan = defaults.plan();
                REQUIRE(enginePlan.has_value());
                REQUIRE(defaultPlan.has_value());
                CHECK(detail::describePlan(engine.declarations(), *enginePlan) == detail::describePlan(defaults.declarations(), *defaultPlan));
            }
        }
    }

    TEST_CASE("a game's pass and the shell's run after post-processing and before the output passes")
    {
        GraphBuilder builder;
        OverlayShell shell;
        const aegis::GraphBuild game = [](GraphBuilder& graph, const DefaultPassesDesc& desc) {
            addScenePasses(graph, desc);
            graph.add<OverlayPass>("Game Overlay");
        };
        buildFrameGraph(builder, { .gpuDriven = false, .ui = true }, game, shell);

        const auto names = plannedNames(builder);
        CHECK(position(names, "Post Processing") < position(names, "Game Overlay"));
        CHECK(position(names, "Game Overlay") < position(names, "Shell Overlay"));
        CHECK(position(names, "Shell Overlay") < position(names, "UI"));
        CHECK(position(names, "UI") < position(names, "Present"));
    }

    TEST_CASE("a game's hook replaces the scene passes")
    {
        GraphBuilder builder;
        aegis::GameShell shell;
        const aegis::GraphBuild game = [](GraphBuilder& graph, const DefaultPassesDesc& /*desc*/) { graph.add<FinalPass>(); };
        buildFrameGraph(builder, { .gpuDriven = false, .ui = true }, game, shell);

        CHECK(plannedNames(builder) == std::vector<std::string_view>{ "Game Final", "UI", "Present" });
    }
}
