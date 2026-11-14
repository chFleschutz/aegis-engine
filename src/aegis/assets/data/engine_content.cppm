export module aegis.assets:engine_content;
import :asset_id;

/// The assets of the engine's `content/` folder, named in code. These are the IDs in the folder's sidecars,
/// not reserved values: the files are normal assets that the database finds like any other.
export namespace aegis::assets::engine
{
inline constexpr AssetId white{ 0xd458ba241de4545aULL };       // white.png, sRGB
inline constexpr AssetId black{ 0xaa1e14dc04e18a02ULL };       // black.png, sRGB
inline constexpr AssetId normal{ 0xae5c7466854a6d39ULL };      // normal.png, a flat tangent-space normal
inline constexpr AssetId checker{ 0x05a13dd0b08222b4ULL };     // checker.png, the magenta and black fallback texture
inline constexpr AssetId cube{ 0xd8d05a38ac6ef7eeULL };        // cube.obj, the fallback mesh
inline constexpr AssetId pbrTemplate{ 0xd569ac68e5f612cbULL }; // pbr.template, the PBR material template (DefaultAssets::pbrTemplate)
}
