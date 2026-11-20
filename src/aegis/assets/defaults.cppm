export module aegis.assets:defaults;
import std;
import :material;
import :texture;
import aegis.assets.data;
import aegis.rhi;

export namespace aegis::assets
{
/// @brief The parameter names of the default PBR material (DefaultAssets::pbrTemplate, content/pbr.template), in
///        the order of its parameter block (PbrParams in shaders/surfaces/pbr.slang). Use these instead of string
///        literals: a typo then fails to compile instead of silently overriding a parameter the template does not
///        have.
namespace pbr
{
inline constexpr const char* albedo{ "albedo" };
inline constexpr const char* emissive{ "emissive" };
inline constexpr const char* metallic{ "metallic" };
inline constexpr const char* roughness{ "roughness" };
inline constexpr const char* ambientOcclusion{ "ambientOcclusion" };
inline constexpr const char* alpha{ "alpha" };
inline constexpr const char* alphaCutoff{ "alphaCutoff" };
inline constexpr const char* albedoMap{ "albedoMap" };
inline constexpr const char* normalMap{ "normalMap" };
inline constexpr const char* metalRoughnessMap{ "metalRoughnessMap" };
inline constexpr const char* ambientOcclusionMap{ "ambientOcclusionMap" };
inline constexpr const char* emissiveMap{ "emissiveMap" };

inline constexpr std::array parameterNames{ albedo, emissive, metallic, roughness, ambientOcclusion, alpha, alphaCutoff,
    albedoMap, normalMap, metalRoughnessMap, ambientOcclusionMap, emissiveMap };
}

/// @brief An engine material template as its files give it: the template file, parsed, and its surface shader's
///        parameter block as the build reflects it.
struct TemplateSource
{
    std::string name; // The template file, for errors ("pbr.template")
    MaterialTemplateDesc desc;
    SurfaceBlock block;
};

/// @brief The engine content's material templates. The caller reads them (aegis::loadEngineTemplates): native files
///        are aegis.serialize's to parse, which builds on this module.
struct EngineTemplates
{
    TemplateSource pbr;
    TemplateSource unlit;
};

/// @brief The assets every scene can fall back on: what the engine's loadDefaultAssets creates on
///        the old stack, built on the RHI.
struct DefaultAssets
{
    std::shared_ptr<Texture> black;
    std::shared_ptr<Texture> white;
    std::shared_ptr<Texture> normal; // Flat tangent-space normal
    std::shared_ptr<Texture> blackCube;
    std::shared_ptr<Texture> whiteCube;

    /// @brief The parameters named in the pbr namespace (content/pbr.template).
    std::shared_ptr<MaterialTemplate> pbrTemplate;
    /// @brief A colour the lighting pass passes through, unlit (content/unlit.template).
    std::shared_ptr<MaterialTemplate> unlitTemplate;
    std::shared_ptr<Material> pbrMaterial;

    /// @brief A new instance of pbrTemplate with the default material's look (what pbrMaterial is created
    ///        as). Loaders hand every model its own, so editing one model's default material does not
    ///        change another's.
    [[nodiscard]] auto createPbrMaterial(rhi::Device& device, std::uint32_t framesInFlight) const
        -> std::expected<std::shared_ptr<Material>, Error>;

    /// @return ErrorCode::InvalidData, naming the template file and the parameter, if a template does not match its
    ///         surface shader (MaterialTemplate::create) or the PBR template lacks a pbr parameter.
    [[nodiscard]] static auto create(rhi::Device& device, std::uint32_t framesInFlight, const EngineTemplates& templates)
        -> std::expected<DefaultAssets, Error>;
};
}
