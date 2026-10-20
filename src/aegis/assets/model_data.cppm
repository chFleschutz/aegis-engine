export module aegis.assets:model_data;
import std;
import :error;
import :image_data;
import :mesh_data;
import aegis.math;

// CPU-side model import: file bytes in, plain data out. Nothing here touches the GPU (no aegis.rhi types) or
// the scene registry, so importing runs on any thread and is testable without a device. Loader turns a
// ModelData into GPU objects and entities.

export namespace aegis::assets
{
enum class AlphaMode
{
    Opaque, // Alpha is ignored
    Mask,   // Fragments with alpha below ModelMaterial::alphaCutoff are discarded
    Blend,  // Alpha blends with what is behind
};

/// @brief A decoded image of a model and how its pixels are to be read.
struct ModelImage
{
    std::string name;
    /// @brief Where the image came from, as the file names it: the URI of an external image relative to the
    ///        model file, or empty for an embedded one (data URI, GLB chunk).
    std::string uri;
    bool srgb{ false }; // Color data (base color, emissive) is sRGB; normals, metal/roughness and occlusion are linear
    ImageData data;
};

/// @brief A material's PBR inputs. Images are indices into ModelData::images; none means "not set"
///        (including a texture that could not be loaded).
struct ModelMaterial
{
    std::string name;
    glm::vec4 baseColor{ 1.0f }; // Linear RGBA factor
    float metallic{ 1.0f };
    float roughness{ 1.0f };
    glm::vec3 emissive{ 0.0f };
    std::optional<std::uint32_t> baseColorImage;
    std::optional<std::uint32_t> metalRoughnessImage;
    std::optional<std::uint32_t> normalImage;
    std::optional<std::uint32_t> occlusionImage;
    std::optional<std::uint32_t> emissiveImage;
    AlphaMode alphaMode{ AlphaMode::Opaque };
    float alphaCutoff{ 0.5f }; // Only meaningful for AlphaMode::Mask
    bool doubleSided{ false };
};

/// @brief One drawable part of a mesh.
struct ModelPrimitive
{
    MeshData mesh;
    std::optional<std::uint32_t> material; // Index into ModelData::materials; none means the default material
};

struct ModelMesh
{
    std::string name;
    std::vector<ModelPrimitive> primitives; // At least one
};

/// @brief A node of the hierarchy with its local transform.
struct ModelNode
{
    std::string name;
    glm::vec3 translation{ 0.0f };
    glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3 scale{ 1.0f };
    std::optional<std::uint32_t> mesh;   // Index into ModelData::meshes
    std::optional<std::uint32_t> parent; // Index into ModelData::nodes; none for a root. Always lower than this node's own index.
};

/// @brief Everything a model file holds, decoded and converted to the engine's conventions (plain values,
///        no GPU state). A cacheable intermediate: Loader::load uploads it.
struct ModelData
{
    std::string name; // The scene's name, or the file's stem
    /// @brief Rotation of the model's root: the file's up axis to the engine's (glTF is Y-up, the engine
    ///        Z-up). Identity if the file already is.
    glm::quat rootRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
    /// @brief A mesh that belongs to the root itself, for formats without a hierarchy (OBJ).
    std::optional<std::uint32_t> rootMesh;
    std::vector<ModelMesh> meshes;
    std::vector<ModelMaterial> materials;
    std::vector<ModelImage> images;
    /// @brief The nodes of one scene in depth-first preorder, so a parent precedes its children.
    std::vector<ModelNode> nodes;
};

/// @brief Imports a glTF / GLB document held in memory.
/// @param directory Where external buffers and images are looked up (the model file's directory). Data URIs
///        and GLB chunks need none.
/// @param name Names the model in messages, and is used if the scene has no name.
/// @note Only the default scene is imported (the first one if the file names none). Triangle lists, strips and
///       fans are imported (strips and fans converted to lists); other topologies are rejected. Missing
///       normals are generated. A missing or undecodable texture is logged with aegis::log::warn and left out
///       of its material; the model still imports. Textures only the unused parts of the file refer to are not
///       decoded.
/// @return ErrorCode::DecodeFailed if the document cannot be parsed, ErrorCode::UnsupportedFormat for an
///         unsupported topology, ErrorCode::InvalidData for inconsistent data (no scenes, a missing node,
///         a node reached twice, an accessor of the wrong type, no POSITION).
[[nodiscard]] auto importGltf(std::span<const std::byte> bytes, const std::filesystem::path& directory = {},
    std::string_view name = {}) -> std::expected<ModelData, Error>;

/// @brief Imports a Wavefront OBJ from a stream. All shapes become one mesh on the root, with the default
///        material (.mtl files are ignored). The v texture coordinate is flipped to a top-left origin.
///        Corners without a normal get flat normals.
/// @param name Names the model, usually the file's stem.
/// @return ErrorCode::DecodeFailed if the stream is not a valid OBJ, ErrorCode::InvalidData if it has no faces.
[[nodiscard]] auto importObj(std::istream& stream, std::string_view name = {}) -> std::expected<ModelData, Error>;

/// @brief Reads a .gltf / .glb / .obj file (by extension, case-insensitive) and imports it.
/// @return ErrorCode::FileNotFound if 'path' does not exist, ErrorCode::UnsupportedFormat for another
///         extension, otherwise the errors of importGltf / importObj with the path added.
[[nodiscard]] auto importModel(const std::filesystem::path& path) -> std::expected<ModelData, Error>;
}
