export module aegis.serialize:material;
import std;
import aegis.reflect;
import :error;

// Material files (docs/asset-system.md, "Native files"):
//
//     {
//       "type": "Material",
//       "id": "5b0e17d2c94af3a1",
//       "version": 1,
//       "template": "70bd2ef0aa4b5f3c",
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
export namespace aegis::serialize
{
inline constexpr std::uint32_t materialVersion = 1;

struct MaterialFile
{
    std::uint64_t id{ 0 };
    std::uint64_t templateId{ 0 };
    reflect::Node parameters{ reflect::Node::makeObject() }; // An object: one member per parameter
};

// Reads a material file and checks its layout and the shape of every value. Errors carry their line.
[[nodiscard]] auto parseMaterial(std::string_view text) -> std::expected<MaterialFile, Error>;

// The material file of `material`, parameters sorted by name: the same input gives the same bytes.
[[nodiscard]] auto writeMaterial(const MaterialFile& material) -> std::string;
}
