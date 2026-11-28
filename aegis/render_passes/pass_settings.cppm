export module aegis.render_passes:pass_settings;
import std;
import aegis.reflect;
import :gpu_types;
import :lighting_pass;
import :post_effects;

// The save-file names of the lighting pass's view modes
export template<>
struct aegis::reflect::EnumNames<aegis::render_graph::LightingViewMode>
{
    using Mode = aegis::render_graph::LightingViewMode;
    static constexpr auto values = std::to_array<std::pair<std::string_view, Mode>>({
        { "SceneColor", Mode::SceneColor },
        { "Albedo", Mode::Albedo },
        { "AmbientOcclusion", Mode::AmbientOcclusion },
        { "Roughness", Mode::Roughness },
        { "Metallic", Mode::Metallic },
        { "Emissive", Mode::Emissive },
        { "ShadowCascades", Mode::ShadowCascades },
    });
};

export namespace aegis::render_graph
{
/// @brief The pass settings of the engine's passes, grouped: one object that outlives the frame graphs
///        (DefaultPassesDesc::settings), so a rebuilt graph keeps what was set and every view's passes read the same.
struct RenderSettings
{
    TemporalAaPass::Settings temporalAa; // Read by each main view's TemporalAaPass and SceneUpdatePass
    LightingPass::Settings lighting;
    BloomPass::Settings bloom;
};

/// @brief The Settings types of the engine's passes (TemporalAaPass::Settings, LightingPass::Settings,
///        BloomPass::Settings), registered in the reflect::TypeCategory::Settings category: what
///        RenderPass::settings names. Built on first use.
[[nodiscard]] auto passSettingsTypes() -> const reflect::TypeRegistry&;
}
