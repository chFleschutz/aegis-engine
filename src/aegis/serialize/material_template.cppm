export module aegis.serialize:material_template;
import std;
import aegis.assets;
import :error;

// Material template files (docs/asset-system.md, "Native files"):
//
//     {
//       "type": "MaterialTemplate",
//       "id": "d569ac68e5f612cb",
//       "version": 1,
//       "surface": "pbr",
//       "parameters": [
//         { "name": "albedo", "type": "vec3", "default": [1, 1, 1] },
//         { "name": "albedoMap", "type": "texture", "default": "white" }
//       ]
//     }
//
// - `surface` names the surface shader, shaders/surfaces/<surface>.slang: letters, digits and underscores.
// - `parameters` lists the parameters in the order of the surface shader's parameter block. A type is int, uint,
//   float, vec2, vec3, vec4 or texture; a default is a number, an array of as many numbers as the vector has, or,
//   for a texture, one of DefaultAssets' solid colours: "white", "black" or "normal".
//
// And the surface shader's block as the build reflects it (aegis_add_shaders, shaders/surfaces/<name>.layout.json):
// slangc's -reflection-json of cmake/AegisSurfaceLayout.slang, whose push constant points at the block.
export namespace aegis::serialize
{
inline constexpr std::uint32_t templateVersion = 1;

struct TemplateFile
{
    std::uint64_t id{ 0 };
    assets::MaterialTemplateDesc desc;
};

// Reads a material template file and checks its layout, every parameter's type and the default's shape. Errors
// carry their line.
[[nodiscard]] auto parseTemplate(std::string_view text) -> std::expected<TemplateFile, Error>;

// Reads a surface shader's reflected parameter block: each field's name, offset, size and type (none for a type no
// material parameter has), and the block's size. Fails with InvalidData on JSON that is not such a reflection.
[[nodiscard]] auto parseSurfaceLayout(std::string_view text) -> std::expected<assets::SurfaceBlock, Error>;
}
