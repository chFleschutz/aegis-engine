#include <doctest/doctest.h>

import std;
import aegis.reflect;
import aegis.render_passes;

namespace
{
using namespace aegis::render_graph;
namespace reflect = aegis::reflect;

const FrameSceneSource scene; // Never bound: the passes are only constructed
}

TEST_SUITE("render_graph::passSettingsTypes")
{
    TEST_CASE("registers each pass's settings in the Settings category")
    {
        const auto& types = passSettingsTypes();
        for (const auto* type : { types.find<LightingPass::Settings>(), types.find<BloomPass::Settings>() })
        {
            REQUIRE(type != nullptr);
            CAPTURE(type->name);
            CHECK(type->category == reflect::TypeCategory::Settings);
            CHECK_FALSE(type->fields.empty());
        }
    }

    TEST_CASE("sets the lighting pass's settings by name, without the editor")
    {
        GraphBuilder builder;
        auto& pass = builder.add<LightingPass>(scene);
        const auto settings = pass.settings();
        REQUIRE(settings.has_value());

        REQUIRE(settings->type.setText(settings->object, "ambientOcclusionFactor", "0.25").has_value());
        REQUIRE(settings->type.setText(settings->object, "viewMode", "ShadowCascades").has_value());
        CHECK(pass.lightingSettings().ambientOcclusionFactor == doctest::Approx(0.25f));
        CHECK(pass.lightingSettings().viewMode == LightingViewMode::ShadowCascades);
        CHECK_FALSE(settings->type.setText(settings->object, "viewMode", "Wireframe").has_value());
    }

    TEST_CASE("sets the bloom pass's settings by name, without the editor")
    {
        GraphBuilder builder;
        auto& pass = builder.add<BloomPass>(scene);
        const auto settings = pass.settings();
        REQUIRE(settings.has_value());

        REQUIRE(settings->type.set(settings->object, "upsampleScale", reflect::Value{ 1.5f }).has_value());
        CHECK(pass.bloomSettings().upsampleScale == doctest::Approx(1.5f));
        CHECK(pass.bloomSettings().downsampleScale == doctest::Approx(1.0f));
    }

    TEST_CASE("a pass without tunables has no settings")
    {
        GraphBuilder builder;
        CHECK_FALSE(builder.add<TransparentPass>(scene).settings().has_value());
    }
}
