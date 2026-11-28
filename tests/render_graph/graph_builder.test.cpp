#include <doctest/doctest.h>

import std;
import aegis.render_graph;
import aegis.rhi;

namespace
{
using namespace aegis::render_graph;
using aegis::rhi::Format;

// A class pass creating "SceneColor", for the callback passes to read.
class ScenePass final : public RenderPass
{
public:
    explicit ScenePass(ResourceDeclarations& declarations) :
        m_color{ declarations.addTexture("SceneColor", Usage::ColorAttachment, { .format = Format::RGBA8_UNORM }) }
    {
    }

    [[nodiscard]] auto name() const -> std::string_view override { return "Scene"; }
    auto execute(ResourcePool& /*pool*/, const FrameInfo& /*frameInfo*/) -> void override {}

private:
    ResourceId m_color;
};

// Adds a callback pass copying "SceneColor" into a texture "Copy" it creates.
auto addCopyPass(GraphBuilder& graph) -> CallbackPass&
{
    return graph.addPass("Copy", [](ResourceDeclarations& declarations) {
        const auto source = declarations.addReference("SceneColor", Usage::TransferSrc);
        const auto copy = declarations.addTexture("Copy", Usage::TransferDst, { .format = Format::RGBA8_UNORM });
        return [source, copy](ResourcePool& /*pool*/, const FrameInfo& /*frameInfo*/) {
            static_cast<void>(source);
            static_cast<void>(copy);
        };
    });
}
}

TEST_SUITE("render_graph::GraphBuilder::addPass")
{
    TEST_CASE("plans a callback pass like a class pass")
    {
        GraphBuilder graph;
        // Declared before the pass creating what it reads: plan() orders it after.
        auto& copy = addCopyPass(graph);
        graph.add<ScenePass>();

        CHECK(copy.name() == "Copy");
        REQUIRE(graph.addedPasses().size() == 2);
        CHECK(graph.addedPasses()[0].get() == &copy);

        const auto plan = graph.plan();
        REQUIRE_MESSAGE(plan.has_value(), plan.error().message);
        CHECK(detail::describePlan(graph.declarations(), *plan) ==
              "1. Scene\n"
              "    create SceneColor ColorAttachment\n"
              "    barrier SceneColor: undefined -> ColorAttachment\n"
              "2. Copy\n"
              "    use    SceneColor TransferSrc\n"
              "    create Copy TransferDst\n"
              "    barrier SceneColor: ColorAttachment -> TransferSrc\n"
              "    barrier Copy: undefined -> TransferDst\n"
              "resources\n"
              "    Copy: texture RGBA8_UNORM\n"
              "    SceneColor: texture RGBA8_UNORM\n");
    }

    TEST_CASE("names a callback pass inside a scope after it, and its resources too")
    {
        GraphBuilder graph;
        graph.add<ScenePass>();
        graph.beginScope("Main");
        auto& copy = addCopyPass(graph);
        graph.endScope();

        // name() stays the pass's own; the declarations, which plans, timings and the renderer panel show, scope it.
        CHECK(copy.name() == "Copy");
        CHECK(graph.declarations().passName(1) == "Main/Copy");
        CHECK(graph.declarations().find("Main/Copy").has_value());

        const auto plan = graph.plan();
        REQUIRE_MESSAGE(plan.has_value(), plan.error().message);
        CHECK(detail::describePlan(graph.declarations(), *plan).contains("2. Main/Copy\n"
                                                                         "    use    SceneColor TransferSrc\n"
                                                                         "    create Main/Copy TransferDst\n"));
    }
}
