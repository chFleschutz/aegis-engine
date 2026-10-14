module;
#include <cstdint>
#include <expected>
#include <memory>

export module aegis.assets:defaults;
import :error;
import :material;
import :texture;
import aegis.rhi;

export namespace aegis::assets
{
/// @brief The assets every scene can fall back on: what the engine's loadDefaultAssets creates on
///        the old stack, built on the RHI.
struct DefaultAssets
{
    std::shared_ptr<Texture> black;
    std::shared_ptr<Texture> white;
    std::shared_ptr<Texture> normal; // Flat tangent-space normal
    std::shared_ptr<Texture> blackCube;
    std::shared_ptr<Texture> whiteCube;

    /// @brief albedo, emissive, metallic, roughness, ambientOcclusion and their maps (albedoMap,
    ///        normalMap, metalRoughnessMap, ambientOcclusionMap, emissiveMap).
    std::shared_ptr<MaterialTemplate> pbrTemplate;
    std::shared_ptr<MaterialInstance> pbrMaterial;

    [[nodiscard]] static auto create(rhi::Device& device, std::uint32_t framesInFlight)
        -> std::expected<DefaultAssets, Error>;
};
}
