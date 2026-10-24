export module aegis.asset_library;

import std;
import aegis.assets;
import aegis.reflect;
import aegis.rhi;
import aegis.world;

export namespace aegis
{
/// @brief Loads assets by ID and keeps what it loaded: one slot per ID, so loading an ID twice returns the
///        same asset and the parts of a model file (its meshes, materials, textures) are shared with
///        anything that loads them by their own ID.
///
/// Where an ID lives comes from the AssetDatabase (the project's assets and the engine content): foreign files
/// load through their importers, material (`.material`) and scene (`.scene`) files through aegis.serialize. Loading is
/// async-shaped and synchronous for now: a returned AssetRef is ready or failed. A failed or missing asset
/// is logged once and replaced by engine content (the checker texture, the default cube, a material with the
/// checker), so a scene with a broken reference still draws.
///
/// A loaded asset is shared: changing it (a material's parameters, say) changes it for every user, in every
/// scene of this library. To change one object only, give it its own asset first (copyMaterial,
/// createMaterial).
///
/// Failures of setup are returned; failures of loading are not (see AssetRef::state).
class AssetLibrary
{
public:
    /// @brief The folders the library reads.
    struct Sources
    {
        std::filesystem::path project;       // Its content/ folder is scanned; sidecars are created for new sources
        std::filesystem::path engineContent; // Read-only
        std::filesystem::path indexFile;     // The database's index cache; empty for none
        std::filesystem::path importCache;   // The import cache's folder (assets::ImportCache); empty for none

        /// @brief The sample project (the samples/ submodule), the engine's content folder, and the index and the
        ///        import cache in the build tree (see aegis.paths).
        [[nodiscard]] static auto standard() -> Sources;
    };

    /// @brief Scans 'sources' and creates the default assets and the IBL pipelines.
    [[nodiscard]] static auto create(rhi::Device& device, std::uint32_t framesInFlight, const Sources& sources = Sources::standard())
        -> std::expected<AssetLibrary, std::string>;

    /// @brief The ID of the asset made from a file of the project ('path' relative to the project's content/ folder,
    ///        with '/') and a sub-asset key ("" for the file's main asset).
    [[nodiscard]] auto find(std::string_view path, std::string_view key = {}) const -> std::optional<assets::AssetId>;

    /// @brief Like find, for code that has nothing to fall back on: an unknown path is logged and exits the
    ///        process, like the engine's other setup errors.
    [[nodiscard]] auto id(std::string_view path, std::string_view key = {}) const -> assets::AssetId;

    /// @brief The asset 'id' names, loaded on first use. Types: assets::Texture, assets::StaticMesh,
    ///        assets::Material (a part of a model file, or a material file), assets::Environment and assets::Model.
    template<class T>
    [[nodiscard]] auto load(assets::AssetId id) -> assets::AssetRef<T>;

    /// @brief The typed forms of load (the examples avoid naming the assets namespace, which a local called
    ///        'assets' hides).
    auto loadTexture(assets::AssetId id) -> assets::AssetRef<assets::Texture>;
    auto loadMesh(assets::AssetId id) -> assets::AssetRef<assets::StaticMesh>;
    auto loadMaterial(assets::AssetId id) -> assets::AssetRef<assets::Material>;
    auto loadEnvironment(assets::AssetId id) -> assets::AssetRef<assets::Environment>;
    auto loadModel(assets::AssetId id) -> assets::AssetRef<assets::Model>;

    /// @brief Adds the file at 'path' in the project's content folder, written after the scan (a scene the editor
    ///        saved under a new name), to the database, or reads it again if it was there.
    /// @return The ID of its main asset, or why the file is no asset (assets::AssetDatabase::addFile).
    [[nodiscard]] auto addFile(std::string_view path) -> std::expected<assets::AssetId, std::string>;

    /// @brief Reads the scene file 'scene' (or a model file's scene, see below) and creates its entities in 'world'
    ///        with the IDs the file gives them (a world's root scene keeps its local IDs). 'types' names the components and scripts the file may
    ///        hold. The assets a MeshRenderer or Sky names are loaded before the component joins its entity, so
    ///        observers of the world (the draw batches) see it complete. A Sky without an environment gets
    ///        defaultEnvironment(); a mesh renderer without a mesh or material is not drawn. The scenes its
    ///        SceneInstance components name are instantiated under their holders (see the overload below).
    /// @return Why the scene could not be read or instantiated (the file and line); 'world' is then unchanged.
    [[nodiscard]] auto instantiateScene(world::World& world, const reflect::TypeRegistry& types, assets::AssetId scene)
        -> std::expected<void, std::string>;

    /// @brief Creates an instance of the scene file or model file 'scene' in 'world' (a prefab, a model, or a
    ///        scene a script spawns): a new entity with a random ID, named after the file, holds it as a child of
    ///        'parent' (a root entity for a null 'parent'), and the scene's entities derive their IDs from the
    ///        holder's (serialize::deriveEntityId). Instantiating a scene twice gives two sets of entities that
    ///        share their meshes and materials. Scene files are read once and kept.
    ///
    ///        A model file's main asset is the scene of its nodes (serialize::modelScene): its entities' local IDs
    ///        are kept in its sidecar next to its parts' IDs, so they survive reimports. A model that fails to
    ///        load is replaced by the default cube.
    /// @return The holder, or why the scene could not be read or instantiated; 'world' is then unchanged.
    [[nodiscard]] auto instantiateScene(world::World& world, const reflect::TypeRegistry& types, assets::AssetId scene, world::Entity parent)
        -> std::expected<world::Entity, std::string>;

    /// @brief Loads the asset references of 'component', an instance of 'type', that are IDs without a slot: what
    ///        instantiateScene does to each component it reads (a MeshRenderer's mesh and material, a Sky's
    ///        environment, a SceneInstance's scene). The editor calls it after assigning an asset by ID.
    void resolve(const reflect::TypeInfo& type, void* component, const reflect::TypeRegistry& types);

    /// @brief Registers an asset made at run time under a new ID, so it can be referenced like a loaded one.
    ///        It is not in the database: nothing finds it by path and a scene file could not load it again.
    template<class T>
    auto add(std::shared_ptr<T> asset) -> assets::AssetRef<T>;

    /// @brief A new instance of 'materialTemplate' (by default the PBR template).
    auto createMaterial(std::shared_ptr<const assets::MaterialTemplate> materialTemplate = nullptr)
        -> assets::AssetRef<assets::Material>;

    /// @brief A copy of 'material' (its template and parameters) under a new ID, for an object that changes
    ///        its material without changing the shared one. A null 'material' gives a new PBR material.
    auto copyMaterial(const assets::AssetRef<assets::Material>& material) -> assets::AssetRef<assets::Material>;

    /// @brief Scans the asset folders again, picking up files added, changed or removed since (new sources get
    ///        sidecars). Assets already loaded stay as they are (no hot reload); on failure the database is unchanged.
    [[nodiscard]] auto rescan() -> std::expected<void, std::string>;

    [[nodiscard]] auto defaults() const -> const assets::DefaultAssets& { return *m_defaults; }
    [[nodiscard]] auto database() const -> const assets::AssetDatabase& { return *m_database; }
    [[nodiscard]] auto importCache() const -> const assets::ImportCache& { return *m_importCache; }

    /// @brief A black sky; scenes replace it with an environment loaded by ID.
    [[nodiscard]] auto defaultEnvironment() -> assets::AssetRef<assets::Environment>;

    AssetLibrary(AssetLibrary&&) noexcept;
    auto operator=(AssetLibrary&&) noexcept -> AssetLibrary&;
    ~AssetLibrary();

private:
    class Impl;
    explicit AssetLibrary(std::unique_ptr<Impl> impl);

    // Registers a run-time asset in the slot cache of its type.
    auto addTexture(std::shared_ptr<assets::Texture> asset) -> assets::AssetRef<assets::Texture>;
    auto addMesh(std::shared_ptr<assets::StaticMesh> asset) -> assets::AssetRef<assets::StaticMesh>;
    auto addMaterial(std::shared_ptr<assets::Material> asset) -> assets::AssetRef<assets::Material>;
    auto addEnvironment(std::shared_ptr<assets::Environment> asset) -> assets::AssetRef<assets::Environment>;
    auto addModel(std::shared_ptr<assets::Model> asset) -> assets::AssetRef<assets::Model>;

    std::unique_ptr<Impl> m_impl;
    const assets::DefaultAssets* m_defaults;
    const assets::AssetDatabase* m_database;
    const assets::ImportCache* m_importCache;
};

template<class T>
auto AssetLibrary::load(assets::AssetId id) -> assets::AssetRef<T>
{
    if constexpr (std::same_as<T, assets::Texture>)
        return loadTexture(id);
    else if constexpr (std::same_as<T, assets::StaticMesh>)
        return loadMesh(id);
    else if constexpr (std::same_as<T, assets::Material>)
        return loadMaterial(id);
    else if constexpr (std::same_as<T, assets::Environment>)
        return loadEnvironment(id);
    else if constexpr (std::same_as<T, assets::Model>)
        return loadModel(id);
    else
        static_assert(!sizeof(T), "AssetLibrary cannot load this asset type");
}

template<class T>
auto AssetLibrary::add(std::shared_ptr<T> asset) -> assets::AssetRef<T>
{
    if constexpr (std::same_as<T, assets::Texture>)
        return addTexture(std::move(asset));
    else if constexpr (std::same_as<T, assets::StaticMesh>)
        return addMesh(std::move(asset));
    else if constexpr (std::same_as<T, assets::Material>)
        return addMaterial(std::move(asset));
    else if constexpr (std::same_as<T, assets::Environment>)
        return addEnvironment(std::move(asset));
    else if constexpr (std::same_as<T, assets::Model>)
        return addModel(std::move(asset));
    else
        static_assert(!sizeof(T), "AssetLibrary cannot hold this asset type");
}
}
