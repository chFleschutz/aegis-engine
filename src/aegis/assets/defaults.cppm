export module aegis.assets:defaults;
import std;
import :error;
import :material;
import :texture;
import aegis.rhi;

export namespace aegis::assets
{
/// @brief The parameter names of the default PBR material (DefaultAssets::pbrTemplate), in the order of
///        its parameter block (PbrMaterialBlock, common::PbrMaterial in shaders/modules/common.slang).
///        Use these instead of string literals: a typo then fails to compile instead of silently
///        overriding a parameter the template does not have.
namespace pbr
{
inline constexpr const char* albedo{ "albedo" };
inline constexpr const char* emissive{ "emissive" };
inline constexpr const char* metallic{ "metallic" };
inline constexpr const char* roughness{ "roughness" };
inline constexpr const char* ambientOcclusion{ "ambientOcclusion" };
inline constexpr const char* albedoMap{ "albedoMap" };
inline constexpr const char* normalMap{ "normalMap" };
inline constexpr const char* metalRoughnessMap{ "metalRoughnessMap" };
inline constexpr const char* ambientOcclusionMap{ "ambientOcclusionMap" };
inline constexpr const char* emissiveMap{ "emissiveMap" };

inline constexpr std::array parameterNames{ albedo, emissive, metallic, roughness, ambientOcclusion, albedoMap,
    normalMap, metalRoughnessMap, ambientOcclusionMap, emissiveMap };
}

/// @brief The assets every scene can fall back on: what the engine's loadDefaultAssets creates on
///        the old stack, built on the RHI.
struct DefaultAssets
{
    std::shared_ptr<Texture> black;
    std::shared_ptr<Texture> white;
    std::shared_ptr<Texture> normal; // Flat tangent-space normal
    std::shared_ptr<Texture> blackCube;
    std::shared_ptr<Texture> whiteCube;

    /// @brief The parameters named in the pbr namespace, laid out as PbrMaterialBlock.
    std::shared_ptr<MaterialTemplate> pbrTemplate;
    std::shared_ptr<Material> pbrMaterial;

    /// @brief A new instance of pbrTemplate with the default material's look (what pbrMaterial is created
    ///        as). Loaders hand every model its own, so editing one model's default material does not
    ///        change another's.
    [[nodiscard]] auto createPbrMaterial(rhi::Device& device, std::uint32_t framesInFlight) const
        -> std::expected<std::shared_ptr<Material>, Error>;

    [[nodiscard]] static auto create(rhi::Device& device, std::uint32_t framesInFlight)
        -> std::expected<DefaultAssets, Error>;
};
}

export namespace aegis::assets::detail
{
/// @brief Adds the default PBR material parameters to materialTemplate in the order of
///        PbrMaterialBlock (see the pbr namespace). The maps default to white, except the normal map.
///        Separate from DefaultAssets::create so the layout can be checked without a Device.
/// @return ErrorCode::InvalidArgument if a parameter exists already or a texture is null.
[[nodiscard]] auto addPbrParameters(MaterialTemplate& materialTemplate, const std::shared_ptr<Texture>& white,
    const std::shared_ptr<Texture>& normal) -> std::expected<void, Error>;
}
