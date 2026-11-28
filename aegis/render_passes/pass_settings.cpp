module;
#include <cassert>

module aegis.render_passes;
import std;
import aegis.reflect;

namespace aegis::render_graph
{
namespace
{
auto registerPassSettings() -> reflect::TypeRegistry
{
    reflect::TypeRegistry types;
    if (auto temporalAa = types.settings<TemporalAaPass::Settings>("TemporalAaSettings"))
    {
        temporalAa->displayName("Temporal AA")
            .field("enabled", &TemporalAaPass::Settings::enabled)
            .field("currentWeight", &TemporalAaPass::Settings::currentWeight, { .widget = reflect::FieldWidget::Slider, .min = 0.01, .max = 1.0 });
        assert(temporalAa->status().has_value());
    }
    if (auto lighting = types.settings<LightingPass::Settings>("LightingSettings"))
    {
        lighting->displayName("Lighting")
            .field("ambientOcclusionFactor", &LightingPass::Settings::ambientOcclusionFactor, { .min = 0.0, .max = 1.0, .step = 0.01 })
            .field("viewMode", &LightingPass::Settings::viewMode);
        assert(lighting->status().has_value());
    }
    if (auto bloom = types.settings<BloomPass::Settings>("BloomSettings"))
    {
        bloom->displayName("Bloom")
            .field("downsampleScale", &BloomPass::Settings::downsampleScale, { .widget = reflect::FieldWidget::Slider, .min = 0.0, .max = 2.0 })
            .field("upsampleScale", &BloomPass::Settings::upsampleScale, { .widget = reflect::FieldWidget::Slider, .min = 0.0, .max = 2.0 });
        assert(bloom->status().has_value());
    }
    return types;
}
}

auto passSettingsTypes() -> const reflect::TypeRegistry&
{
    static const reflect::TypeRegistry types = registerPassSettings();
    return types;
}
}
