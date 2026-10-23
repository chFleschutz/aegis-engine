export module aegis.assets:model_data;
import std;
import :asset_id;
import :error;
import :image_data;
import :mesh_data;
import :sidecar;
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

/// @brief An image of a model and how its materials read its pixels. An image embedded in the file (data URI, GLB
///        chunk) is decoded and belongs to the model. An external image, one the file names by URI, is a file of
///        its own with its own Texture asset and import settings (docs/asset-system.md, "Identity"): the importer
///        records where it is and how it is used, but does not read it.
struct ModelImage
{
    std::string name;
    /// @brief The URI of an external image relative to the model file, or empty for an embedded one.
    std::string uri;
    /// @brief The usage the materials imply: color data (base color, emissive) is sRGB; normals, metal/roughness
    ///        and occlusion are linear. An embedded image is read this way unless its settings say otherwise; an
    ///        external one by its own settings (see textureSrgb).
    bool srgb{ false };
    /// @brief An embedded image's settings as a part of the model, from the model's sidecar (applyEmbeddedImageSettings);
    ///        defaults for an external one, which has settings of its own.
    ImportSettings settings;
    ImageData data; // An embedded image's pixels; empty for an external one
    /// @brief The encoded bytes (PNG, JPEG, ...) of an embedded image, which the import cache keeps; empty for an
    ///        external one.
    std::vector<std::byte> encoded;
    /// @brief The Texture asset of an external image's file, set by resolveExternalImages; invalid for an embedded
    ///        image, before resolving, or if no asset has that file.
    AssetId texture;
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
///        no GPU state). A cacheable intermediate (see ImportCache): Loader::upload uploads it.
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
    /// @brief Files besides the model file whose contents the import depends on, relative to its directory with
    ///        '/': a glTF's external buffers. The import cache hashes them into its key, so changing one reimports
    ///        the model. External images are assets of their own and not listed.
    std::vector<std::string> dependencies;
};

/// @brief Imports a glTF / GLB document held in memory.
/// @param directory Where external buffers are looked up (the model file's directory). Data URIs and GLB chunks
///        need none. External images are recorded by URI and not read (see ModelImage).
/// @param name Names the model in messages, and is used if the scene has no name.
/// @note Only the default scene is imported (the first one if the file names none). Triangle lists, strips and
///       fans are imported (strips and fans converted to lists); other topologies are rejected. Missing
///       normals are generated. An undecodable embedded texture is logged with aegis::log::warn and left out of
///       its material; the model still imports. Textures only the unused parts of the file refer to are left out.
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
