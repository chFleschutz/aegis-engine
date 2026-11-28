#include <doctest/doctest.h>

import std;
import aegis.reflect;
import aegis.render_passes;
import aegis.rhi;

namespace
{
using namespace aegis::render_graph;
namespace reflect = aegis::reflect;

const FrameSceneSource scene; // Never bound: the passes are only constructed
PipelineLibrary pipelines;    // Never asked for a pipeline: no pass creates its resources

constexpr auto targetFormat = aegis::rhi::Format::BGRA8_SRGB; // The swapchain's usual format

// The settings objects of the graph's passes that have any, in the order added.
auto settingsObjects(const GraphBuilder& builder) -> std::vector<void*>
{
    std::vector<void*> objects;
    for (const auto& pass : builder.addedPasses())
        if (const auto settings = pass->settings())
            objects.push_back(settings->object);
    return objects;
}
}

TEST_SUITE("render_graph::passSettingsTypes")
{
    TEST_CASE("registers each pass's settings in the Settings category")
    {
        const auto& types = passSettingsTypes();
        for (const auto* type : { types.find<TemporalAaPass::Settings>(), types.find<LightingPass::Settings>(), types.find<BloomPass::Settings>() })
        {
            REQUIRE(type != nullptr);
            CAPTURE(type->name);
            CHECK(type->category == reflect::TypeCategory::Settings);
            CHECK_FALSE(type->fields.empty());
        }
    }

    TEST_CASE("sets the lighting pass's settings by name, without the editor")
    {
        LightingPass::Settings lighting;
        GraphBuilder builder;
        auto& pass = builder.add<LightingPass>(scene, pipelines, lighting);
        const auto settings = pass.settings();
        REQUIRE(settings.has_value());
        CHECK(settings->object == &lighting);

        REQUIRE(settings->type.setText(settings->object, "ambientOcclusionFactor", "0.25").has_value());
        REQUIRE(settings->type.setText(settings->object, "viewMode", "ShadowCascades").has_value());
        CHECK(lighting.ambientOcclusionFactor == doctest::Approx(0.25f));
        CHECK(lighting.viewMode == LightingViewMode::ShadowCascades);
        CHECK_FALSE(settings->type.setText(settings->object, "viewMode", "Wireframe").has_value());
    }

    TEST_CASE("sets the bloom pass's settings by name, without the editor")
    {
        BloomPass::Settings bloom;
        GraphBuilder builder;
        auto& pass = builder.add<BloomPass>(scene, pipelines, bloom);
        const auto settings = pass.settings();
        REQUIRE(settings.has_value());
        CHECK(settings->object == &bloom);

        REQUIRE(settings->type.set(settings->object, "upsampleScale", reflect::Value{ 1.5f }).has_value());
        CHECK(bloom.upsampleScale == doctest::Approx(1.5f));
        CHECK(bloom.downsampleScale == doctest::Approx(1.0f));
    }

    TEST_CASE("sets the temporal AA pass's settings by name, without the editor")
    {
        TemporalAaPass::Settings temporalAa;
        GraphBuilder builder;
        auto& pass = builder.add<TemporalAaPass>(scene, pipelines, temporalAa);
        const auto settings = pass.settings();
        REQUIRE(settings.has_value());
        CHECK(settings->object == &temporalAa);
        CHECK(settings->type.name == "TemporalAaSettings");

        REQUIRE(settings->type.setText(settings->object, "enabled", "false").has_value());
        REQUIRE(settings->type.set(settings->object, "currentWeight", reflect::Value{ 0.25f }).has_value());
        CHECK_FALSE(temporalAa.enabled);
        CHECK(temporalAa.currentWeight == doctest::Approx(0.25f));
    }

    TEST_CASE("the scene update pass shows no settings: the temporal AA pass shows the ones it jitters by")
    {
        TemporalAaPass::Settings temporalAa;
        GraphBuilder builder;
        CHECK_FALSE(builder.add<SceneUpdatePass>(scene, 0u, &temporalAa).settings().has_value());
        CHECK_FALSE(builder.add<SceneUpdatePass>(scene).settings().has_value());
    }

    TEST_CASE("a pass without tunables has no settings")
    {
        GraphBuilder builder;
        CHECK_FALSE(builder.add<TransparentPass>(scene, pipelines).settings().has_value());
    }

    TEST_CASE("a rebuilt default graph's passes read the same settings, in every view")
    {
        RenderSettings settings;
        const DefaultPassesDesc::View edit{ .name = "Edit" };
        const DefaultPassesDesc::View play{ .name = "Play" };
        {
            GraphBuilder builder;
            addDefaultPasses(builder, { .scene = scene, .pipelines = pipelines, .settings = settings, .gpuDriven = false, .targetFormat = targetFormat, .views = { edit } });
            // As the renderer panel does: through the pass.
            for (const auto& pass : builder.addedPasses())
                if (const auto passSettings = pass->settings(); passSettings && passSettings->type.name == "LightingSettings")
                    REQUIRE(passSettings->type.setText(passSettings->object, "viewMode", "Albedo").has_value());
            for (const auto& pass : builder.addedPasses())
                if (const auto passSettings = pass->settings(); passSettings && passSettings->type.name == "BloomSettings")
                    REQUIRE(passSettings->type.set(passSettings->object, "downsampleScale", reflect::Value{ 0.5f }).has_value());
        }
        CHECK(settings.lighting.viewMode == LightingViewMode::Albedo);
        CHECK(settings.bloom.downsampleScale == doctest::Approx(0.5f));

        // The rebuilt graph, with a second view, starts from what was set: both views' passes read the one object.
        GraphBuilder rebuilt;
        addDefaultPasses(rebuilt, { .scene = scene, .pipelines = pipelines, .settings = settings, .gpuDriven = false, .targetFormat = targetFormat, .views = { edit, play } });
        CHECK(settingsObjects(rebuilt) == std::vector<void*>{ &settings.lighting, &settings.temporalAa, &settings.bloom, &settings.lighting, &settings.temporalAa, &settings.bloom });
        CHECK(settings.lighting.viewMode == LightingViewMode::Albedo);
        CHECK(settings.bloom.downsampleScale == doctest::Approx(0.5f));
    }
}
