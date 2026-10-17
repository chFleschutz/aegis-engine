export module aegis.assets:loader;
import std;
import :error;
import :material;
import aegis.rhi;
import aegis.scene.registry;

export namespace aegis::assets
{
/// @brief Loads a model file into a scene as entities with assets::Mesh and assets::Material.
class Loader
{
public:
    struct Desc
    {
        rhi::Device& device;
        aegis::scene::Registry& scene;
        /// @brief The file's materials become instances of this template (the default PBR template's
        ///        parameter names are expected; see DefaultAssets::pbrTemplate).
        std::shared_ptr<const MaterialTemplate> pbrTemplate;
        /// @brief For primitives without a material.
        std::shared_ptr<MaterialInstance> defaultMaterial;
        std::uint32_t framesInFlight;
    };

    Loader() = delete;

    /// @brief Loads .gltf / .glb (fastgltf) or .obj (tinyobjloader).
    /// @return The root entity. GPU uploads are queued; they complete with the next
    ///         Device::flushUploads.
    [[nodiscard]] static auto load(const Desc& desc, const std::filesystem::path& path)
        -> std::expected<aegis::scene::Entity, Error>;

private:
    [[nodiscard]] static auto loadGltf(const Desc& desc, const std::filesystem::path& path)
        -> std::expected<aegis::scene::Entity, Error>;
    [[nodiscard]] static auto loadObj(const Desc& desc, const std::filesystem::path& path)
        -> std::expected<aegis::scene::Entity, Error>;
};
}
