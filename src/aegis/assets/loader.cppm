export module aegis.assets:loader;
import std;
import :defaults;
import :asset_ref;
import :error;
import :material;
import :model_data;
import :static_mesh;
import aegis.math;
import aegis.rhi;

export namespace aegis::assets
{
/// @brief One primitive of a loaded model: a mesh and the material it is drawn with.
struct ModelGpuPrimitive
{
    AssetRef<StaticMesh> mesh;
    AssetRef<Material> material; // The library gives both their IDs; the loader leaves them null
};

struct ModelGpuMesh
{
    std::string name;
    std::vector<ModelGpuPrimitive> primitives; // At least one
};

/// @brief An embedded texture of a loaded model, with the name its sub-asset key is made from.
struct ModelGpuTexture
{
    std::string name;
    std::shared_ptr<Texture> texture;
};

struct ModelGpuMaterial
{
    std::string name;
    std::shared_ptr<Material> material;
};

/// @brief The GPU objects of one ModelData (what Loader::load builds): meshes with their materials, indexed like
///        ModelData::meshes, and the node hierarchy. Plain shared data: instantiating a Model N times shares
///        its meshes and materials.
struct Model
{
    std::string name;
    glm::quat rootRotation{ 1.0f, 0.0f, 0.0f, 0.0f }; // See ModelData::rootRotation
    std::optional<std::uint32_t> rootMesh;            // Index into meshes; belongs to the root itself
    std::vector<ModelGpuMesh> meshes;
    /// @brief The parts the asset library gives IDs: the file's embedded textures, in ModelData::images order (external
    ///        images are assets of their own), and its materials, indexed like ModelData::materials.
    std::vector<ModelGpuTexture> textures;
    std::vector<ModelGpuMaterial> materials;
    std::vector<ModelNode> nodes; // Depth-first preorder; node.mesh indexes meshes
};

/// @brief Loads a model file into GPU objects.
class Loader
{
public:
    struct Desc
    {
        rhi::Device& device;
        /// @brief The file's materials become instances of its pbrTemplate (the default PBR template's
        ///        parameter names are expected; see DefaultAssets::pbrTemplate). Primitives without a
        ///        material share one new instance of DefaultAssets::createPbrMaterial per load.
        const DefaultAssets& defaults;
        std::uint32_t framesInFlight;
        /// @brief The texture of an external image (ModelImage::uri set), the asset library's texture of the image's
        ///        own asset. None, or a null texture, leaves the image out of its materials.
        std::function<std::shared_ptr<Texture>(const ModelImage&)> externalTexture{};
    };

    Loader() = delete;

    /// @brief Imports a .gltf / .glb or .obj file (see importModel) and uploads its textures, meshes and
    ///        materials. External images without Desc::externalTexture are read from the model's folder, with the
    ///        usage the materials imply; one that cannot be read is logged and left out.
    /// @return The model. GPU uploads are queued; they complete with the next Device::flushUploads.
    [[nodiscard]] static auto load(const Desc& desc, const std::filesystem::path& path) -> std::expected<Model, Error>;

    /// @brief Uploads the textures, meshes and materials of an imported model (importModel, ImportCache::loadModel).
    /// @return The model. GPU uploads are queued; they complete with the next Device::flushUploads.
    [[nodiscard]] static auto upload(const Desc& desc, const ModelData& model) -> std::expected<Model, Error>;
};
}
