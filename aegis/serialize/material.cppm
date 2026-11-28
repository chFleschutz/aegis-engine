export module aegis.serialize:material;
import std;
import aegis.assets;
import aegis.reflect;
import aegis.rhi;
import :error;
import :migration;

// Material files (docs/asset-system.md, "Native files"):
//
//     {
//       "type": "Material",
//       "id": "5b0e17d2c94af3a1",
//       "version": 1,
//       "template": "70bd2ef0aa4b5f3c",
//       "alphaMode": "mask",
//       "cullMode": "none",
//       "parameters": {
//         "albedoMap": "08e3f5b6a1c47d29",
//         "metallic": 1,
//         "roughness": 0.4
//       }
//     }
//
// - `template` is the asset ID of the material template (the engine content's PBR template until custom
//   shaders), `parameters` the values that override its defaults, written sorted by name.
// - A value is a number, an array of 2 to 4 numbers (a vector) or a texture's asset ID. Which one a parameter
//   takes is the template's to say, so the library checks the values against it when it loads the material.
// - `alphaMode` ("opaque", "mask", "blend") and `cullMode` ("back", "none", "front") pick the material's variant;
//   each is optional and written only when it is not the default (opaque, back).
// - An older `version` is upgraded on load (materialMigrations); writeMaterial writes the current one.
export namespace aegis::serialize
{
inline constexpr std::uint32_t materialVersion = 1;

// The upgrades of material files to materialVersion, one per older version (none yet: version 1 is the first).
// Raising materialVersion means adding the step from the version before it.
[[nodiscard]] auto materialMigrations() -> const FileMigrations&;

struct MaterialFile
{
    std::uint64_t id{ 0 };
    std::uint64_t templateId{ 0 };
    assets::AlphaMode alphaMode{ assets::AlphaMode::Opaque };
    rhi::CullMode cullMode{ rhi::CullMode::Back };           // Back, None or Front
    reflect::Node parameters{ reflect::Node::makeObject() }; // An object: one member per parameter
};

// Reads a material file, upgraded to materialVersion, and checks its layout and the shape of every value. Errors
// carry their line.
[[nodiscard]] auto parseMaterial(std::string_view text) -> std::expected<MaterialFile, Error>;

// The material file of `material`, parameters sorted by name: the same input gives the same bytes.
[[nodiscard]] auto writeMaterial(const MaterialFile& material) -> std::string;
}
