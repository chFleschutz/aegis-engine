module;

#include <cstdlib>

module aegis.asset_library;

import std;
import aegis.base;
import aegis.instantiate;
import aegis.paths;

namespace aegis
{
using assets::AssetId;
using assets::AssetRef;
using assets::AssetSlot;
using assets::AssetState;

namespace
{
/// The loaded assets of one type by ID.
template<class T>
struct Cache
{
    std::unordered_map<AssetId, std::shared_ptr<AssetSlot<T>>> slots;

    /// The slot of 'id' once it has been filled.
    auto find(AssetId id) const -> std::optional<AssetRef<T>>
    {
        if (const auto it = slots.find(id); it != slots.end() && it->second->state != AssetState::Pending)
            return AssetRef<T>{ id, it->second };
        return std::nullopt;
    }

    auto slot(AssetId id) -> const std::shared_ptr<AssetSlot<T>>&
    {
        auto& slot = slots[id];
        if (!slot)
            slot = std::make_shared<AssetSlot<T>>();
        return slot;
    }

    auto fill(AssetId id, std::shared_ptr<T> value) -> AssetRef<T>
    {
        const auto& s = slot(id);
        s->value = std::move(value);
        s->state = AssetState::Ready;
        s->message.clear();
        return AssetRef<T>{ id, s };
    }
};

auto readFile(const std::filesystem::path& path) -> std::optional<std::string>
{
    std::ifstream stream{ path, std::ios::binary };
    if (!stream)
        return std::nullopt;
    return std::string{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
}

template<class T, class E>
auto check(std::expected<T, E> result, const std::string& what) -> T
{
    if (!result)
    {
        if constexpr (requires { describe(result.error()); })
            log::fatal("Failed to {}: {}", what, describe(result.error()));
        else
            log::fatal("Failed to {}: {}", what, result.error().message);
        std::exit(EXIT_FAILURE);
    }
    return std::move(*result);
}
}

class AssetLibrary::Impl
{
public:
    Impl(rhi::Device& device, std::uint32_t framesInFlight, assets::DefaultAssets defaults, render_graph::Ibl ibl,
        assets::AssetDatabase database) :
        m_device{ &device },
        m_framesInFlight{ framesInFlight },
        m_defaults{ std::move(defaults) },
        m_ibl{ std::move(ibl) },
        m_database{ std::move(database) }
    {
    }

    [[nodiscard]] auto defaults() const -> const assets::DefaultAssets& { return m_defaults; }
    [[nodiscard]] auto database() const -> const assets::AssetDatabase& { return m_database; }
    [[nodiscard]] auto device() -> rhi::Device& { return *m_device; }
    [[nodiscard]] auto framesInFlight() const -> std::uint32_t { return m_framesInFlight; }

    auto textures() -> Cache<assets::Texture>& { return m_textures; }
    auto meshes() -> Cache<assets::StaticMesh>& { return m_meshes; }
    auto materials() -> Cache<assets::Material>& { return m_materials; }
    auto environments() -> Cache<assets::Environment>& { return m_environments; }
    auto models() -> Cache<assets::Model>& { return m_models; }

    auto loadTexture(AssetId id) -> AssetRef<assets::Texture>
    {
        if (auto ref = m_textures.find(id))
            return *ref;

        const auto* entry = m_database.find(id);
        if (!entry)
            return failTexture(id, "no asset has this ID");
        if (!entry->key.empty())
        {
            loadOwner(*entry);
            if (auto ref = m_textures.find(id))
                return *ref;
            return failTexture(id, std::format("'{}' has no texture '{}'", entry->path, entry->key));
        }
        if (assets::importerFor(entry->path) != "texture")
            return failTexture(id, std::format("'{}' is not a texture file", entry->path));

        const auto path = m_database.absolutePath(*entry);
        const auto srgb = readSidecar(*entry).and_then([](const assets::Sidecar& s) { return s.settings.srgb; }).value_or(true);
        auto texture = assets::Texture::loadFromFile(*m_device, path, srgb ? rhi::Format::RGBA8_SRGB : rhi::Format::RGBA8_UNORM);
        if (!texture)
            return failTexture(id, assets::describe(texture.error()));
        return m_textures.fill(id, std::move(*texture));
    }

    auto loadMesh(AssetId id) -> AssetRef<assets::StaticMesh>
    {
        if (auto ref = m_meshes.find(id))
            return *ref;

        const auto* entry = m_database.find(id);
        if (entry && !entry->key.empty())
        {
            loadOwner(*entry);
            if (auto ref = m_meshes.find(id))
                return *ref;
            return failMesh(id, std::format("'{}' has no mesh '{}'", entry->path, entry->key));
        }
        return failMesh(id, entry ? "it is a file, not a mesh" : "no asset has this ID");
    }

    auto loadMaterial(AssetId id) -> AssetRef<assets::Material>
    {
        if (auto ref = m_materials.find(id))
            return *ref;

        const auto* entry = m_database.find(id);
        if (entry && !entry->key.empty())
        {
            loadOwner(*entry);
            if (auto ref = m_materials.find(id))
                return *ref;
            return failMaterial(id, std::format("'{}' has no material '{}'", entry->path, entry->key));
        }
        return failMaterial(id, entry ? "it is a file, not a material" : "no asset has this ID");
    }

    auto loadEnvironment(AssetId id) -> AssetRef<assets::Environment>
    {
        if (auto ref = m_environments.find(id))
            return *ref;

        const auto* entry = m_database.find(id);
        if (!entry)
            return failEnvironment(id, "no asset has this ID");
        if (!entry->key.empty() || assets::importerFor(entry->path) != "environment")
            return failEnvironment(id, std::format("'{}' is not an environment file", entry->path));

        auto environment = m_ibl.loadEnvironment(m_database.absolutePath(*entry));
        if (!environment)
            return failEnvironment(id, render_graph::describe(environment.error()));
        return m_environments.fill(id, std::move(*environment));
    }

    auto loadModel(AssetId id) -> AssetRef<assets::Model>
    {
        if (auto ref = m_models.find(id))
            return *ref;

        const auto* entry = m_database.find(id);
        if (!entry)
            return failModel(id, "no asset has this ID");
        if (!entry->key.empty() || assets::importerFor(entry->path) != "model")
            return failModel(id, std::format("'{}' is not a model file", entry->path));

        auto model = assets::Loader::load(
            { .device = *m_device, .defaults = m_defaults, .framesInFlight = m_framesInFlight }, m_database.absolutePath(*entry));
        if (!model)
            return failModel(id, assets::describe(model.error()));

        auto shared = std::make_shared<assets::Model>(std::move(*model));
        if (auto registered = registerParts(*entry, *shared); !registered)
            return failModel(id, assets::describe(registered.error()));
        return m_models.fill(id, std::move(shared));
    }

    auto createMaterial(std::shared_ptr<const assets::MaterialTemplate> materialTemplate) -> AssetRef<assets::Material>
    {
        if (!materialTemplate)
            materialTemplate = m_defaults.pbrTemplate;
        auto material = check(assets::Material::create(*m_device, std::move(materialTemplate), m_framesInFlight), "create a material");
        return m_materials.fill(AssetId::generate(), std::move(material));
    }

    auto copyMaterial(const AssetRef<assets::Material>& source) -> AssetRef<assets::Material>
    {
        if (!source)
            return createMaterial(nullptr);
        auto material = check(source->copy(), "copy a material");
        return m_materials.fill(AssetId::generate(), std::move(material));
    }

    auto defaultEnvironment() -> AssetRef<assets::Environment>
    {
        if (!m_defaultEnvironment.valid())
        {
            m_defaultEnvironment = AssetId::generate();
            m_environments.fill(m_defaultEnvironment, blackEnvironment());
        }
        return *m_environments.find(m_defaultEnvironment);
    }

private:
    auto readSidecar(const assets::AssetEntry& entry) const -> std::optional<assets::Sidecar>
    {
        const auto text = readFile(assets::sidecarPath(m_database.absolutePath(entry)));
        if (!text)
            return std::nullopt;
        auto sidecar = assets::parseSidecar(*text);
        if (!sidecar)
            return std::nullopt;
        return std::move(*sidecar);
    }

    // Loads the model file that 'part' belongs to, which fills the slots of all its parts.
    auto loadOwner(const assets::AssetEntry& part) -> void
    {
        if (const auto owner = m_database.find(part.root, part.path))
            loadModel(*owner);
    }

    static auto describeFailure(AssetId id, std::string_view what, const std::string& message) -> std::string
    {
        return std::format("Failed to load {} {}: {}; using a fallback", what, id, message);
    }

    // A failure is logged once, when the slot is first filled; later loads of the ID find the slot.
    template<class T>
    auto fail(Cache<T>& cache, AssetId id, std::string_view what, const std::string& message, std::shared_ptr<T> fallback)
        -> AssetRef<T>
    {
        log::warn("{}", describeFailure(id, what, message));
        const auto& slot = cache.slot(id);
        slot->state = AssetState::Failed;
        slot->message = message;
        slot->value = std::move(fallback);
        return AssetRef<T>{ id, slot };
    }

    auto failTexture(AssetId id, const std::string& message) -> AssetRef<assets::Texture>
    {
        std::shared_ptr<assets::Texture> fallback;
        if (id != assets::engine::checker)
            fallback = loadTexture(assets::engine::checker).shared();
        return fail(m_textures, id, "texture", message, std::move(fallback));
    }

    auto failMesh(AssetId id, const std::string& message) -> AssetRef<assets::StaticMesh>
    {
        return fail(m_meshes, id, "mesh", message, cubeMesh());
    }

    auto failMaterial(AssetId id, const std::string& message) -> AssetRef<assets::Material>
    {
        std::shared_ptr<assets::Material> fallback;
        if (auto material = m_defaults.createPbrMaterial(*m_device, m_framesInFlight))
        {
            fallback = std::move(*material);
            if (const auto checker = loadTexture(assets::engine::checker).shared())
                (void)fallback->setParameter(assets::pbr::albedoMap, checker);
        }
        return fail(m_materials, id, "material", message, std::move(fallback));
    }

    auto failEnvironment(AssetId id, const std::string& message) -> AssetRef<assets::Environment>
    {
        return fail(m_environments, id, "environment", message, blackEnvironment());
    }

    auto failModel(AssetId id, const std::string& message) -> AssetRef<assets::Model>
    {
        std::shared_ptr<assets::Model> fallback;
        if (id != assets::engine::cube)
            fallback = loadModel(assets::engine::cube).shared();
        return fail(m_models, id, "model", message, std::move(fallback));
    }

    // The default cube's mesh, or none if the engine content is missing it.
    auto cubeMesh() -> std::shared_ptr<assets::StaticMesh>
    {
        const auto cube = loadModel(assets::engine::cube);
        if (!cube || cube->meshes.empty() || cube->meshes.front().primitives.empty())
            return nullptr;
        return cube->meshes.front().primitives.front().mesh.shared();
    }

    auto blackEnvironment() const -> std::shared_ptr<assets::Environment>
    {
        return std::make_shared<assets::Environment>(assets::Environment{
            .skybox = m_defaults.blackCube,
            .irradiance = m_defaults.blackCube,
            .prefiltered = m_defaults.blackCube,
        });
    }

    // Gives the parts of a freshly loaded model their IDs from the file's sidecar (new parts get new IDs,
    // recorded in it), fills their slots, and points the model's primitives at those slots.
    auto registerParts(const assets::AssetEntry& entry, assets::Model& model) -> std::expected<void, assets::Error>
    {
        auto sidecar = readSidecar(entry).value_or(assets::Sidecar{ .id = entry.id, .importer = "model" });
        const auto known = sidecar.subAssets.size();
        assets::SubAssetKeys keys{ sidecar.settings.strictNames };

        std::vector<std::pair<std::string, AssetId>> parts;
        const auto addPart = [&](const std::string& key) {
            const auto id = sidecar.subAssetId(key);
            parts.emplace_back(key, id);
            return id;
        };

        std::vector<AssetId> textureIds, materialIds;
        for (std::size_t i = 0; i < model.textures.size(); ++i)
        {
            const auto& texture = model.textures[i];
            const auto name = texture.uri.empty() ? texture.name : std::filesystem::path{ texture.uri }.filename().generic_string();
            auto key = keys.name("Texture", name, i);
            if (!key)
                return std::unexpected{ std::move(key.error()) };
            textureIds.push_back(addPart(*key));
        }
        for (std::size_t i = 0; i < model.materials.size(); ++i)
        {
            auto key = keys.name("Material", model.materials[i].name, i);
            if (!key)
                return std::unexpected{ std::move(key.error()) };
            materialIds.push_back(addPart(*key));
        }
        std::vector<std::vector<AssetId>> meshIds;
        for (std::size_t i = 0; i < model.meshes.size(); ++i)
        {
            auto key = keys.name("StaticMesh", model.meshes[i].name, i);
            if (!key)
                return std::unexpected{ std::move(key.error()) };
            auto& ids = meshIds.emplace_back();
            for (std::size_t j = 0; j < model.meshes[i].primitives.size(); ++j)
                ids.push_back(addPart(assets::SubAssetKeys::withIndex(*key, j)));
        }

        if (sidecar.subAssets.size() != known && !m_database.roots()[entry.root].readOnly)
        {
            const auto path = assets::sidecarPath(m_database.absolutePath(entry));
            if (std::ofstream out{ path, std::ios::binary })
                out << assets::writeSidecar(sidecar);
            else
                log::warn("Cannot write the sidecar '{}'; the parts of this model get new IDs next run", path.generic_string());
        }
        if (auto added = m_database.addSubAssets(entry.root, entry.path, parts); !added)
            return std::unexpected{ std::move(added.error()) };

        for (std::size_t i = 0; i < model.textures.size(); ++i)
            m_textures.fill(textureIds[i], model.textures[i].texture);

        std::unordered_map<const assets::Material*, AssetRef<assets::Material>> materialRefs;
        for (std::size_t i = 0; i < model.materials.size(); ++i)
            materialRefs.emplace(model.materials[i].material.get(), m_materials.fill(materialIds[i], model.materials[i].material));

        for (std::size_t i = 0; i < model.meshes.size(); ++i)
        {
            for (std::size_t j = 0; j < model.meshes[i].primitives.size(); ++j)
            {
                auto& primitive = model.meshes[i].primitives[j];
                primitive.mesh = m_meshes.fill(meshIds[i][j], primitive.mesh.shared());

                // Primitives without a material share the load's default material, which is no part of the file.
                auto [it, inserted] = materialRefs.try_emplace(primitive.material.get(), AssetRef<assets::Material>{});
                if (inserted)
                    it->second = m_materials.fill(AssetId::generate(), primitive.material.shared());
                primitive.material = it->second;
            }
        }
        return {};
    }

    rhi::Device* m_device;
    std::uint32_t m_framesInFlight;
    assets::DefaultAssets m_defaults;
    render_graph::Ibl m_ibl;
    assets::AssetDatabase m_database;
    AssetId m_defaultEnvironment;

    Cache<assets::Texture> m_textures;
    Cache<assets::StaticMesh> m_meshes;
    Cache<assets::Material> m_materials;
    Cache<assets::Environment> m_environments;
    Cache<assets::Model> m_models;
};

auto AssetLibrary::Sources::standard() -> Sources
{
    return { .project = paths::assets(), .engineContent = paths::content(), .indexFile = paths::cache() / "index" };
}

AssetLibrary::AssetLibrary(std::unique_ptr<Impl> impl) :
    m_impl{ std::move(impl) },
    m_defaults{ &m_impl->defaults() },
    m_database{ &m_impl->database() }
{
}

AssetLibrary::AssetLibrary(AssetLibrary&&) noexcept = default;
auto AssetLibrary::operator=(AssetLibrary&&) noexcept -> AssetLibrary& = default;
AssetLibrary::~AssetLibrary() = default;

auto AssetLibrary::create(rhi::Device& device, std::uint32_t framesInFlight, const Sources& sources)
    -> std::expected<AssetLibrary, std::string>
{
    auto database = assets::AssetDatabase::scan({
        .roots = { { sources.project }, { sources.engineContent, true } },
        .indexFile = sources.indexFile,
        .createSidecars = true,
    });
    if (!database)
        return std::unexpected{ std::format("Failed to scan the assets: {}", describe(database.error())) };

    auto defaults = assets::DefaultAssets::create(device, framesInFlight);
    if (!defaults)
        return std::unexpected{ std::format("Failed to create the default assets: {}", describe(defaults.error())) };
    auto ibl = render_graph::Ibl::create(device);
    if (!ibl)
        return std::unexpected{ std::format("Failed to create the IBL pipelines: {}", describe(ibl.error())) };

    return AssetLibrary{ std::make_unique<Impl>(device, framesInFlight, std::move(*defaults), std::move(*ibl), std::move(*database)) };
}

auto AssetLibrary::find(std::string_view path, std::string_view key) const -> std::optional<AssetId>
{
    return m_database->find(0, path, key);
}

auto AssetLibrary::id(std::string_view path, std::string_view key) const -> AssetId
{
    const auto found = find(path, key);
    if (!found)
    {
        log::fatal("No asset '{}'{}{} in the project", path, key.empty() ? "" : " / ", key);
        std::exit(EXIT_FAILURE);
    }
    return *found;
}

auto AssetLibrary::instantiate(world::World& world, AssetId model) -> world::Entity
{
    const auto loaded = m_impl->loadModel(model);
    if (!loaded)
    {
        log::fatal("Cannot instantiate model {}: it did not load and there is no fallback", model);
        std::exit(EXIT_FAILURE);
    }
    return aegis::instantiate(world.registry(), *loaded);
}

auto AssetLibrary::createMaterial(std::shared_ptr<const assets::MaterialTemplate> materialTemplate) -> AssetRef<assets::Material>
{
    return m_impl->createMaterial(std::move(materialTemplate));
}

auto AssetLibrary::copyMaterial(const AssetRef<assets::Material>& material) -> AssetRef<assets::Material>
{
    return m_impl->copyMaterial(material);
}

auto AssetLibrary::defaultEnvironment() -> AssetRef<assets::Environment>
{
    return m_impl->defaultEnvironment();
}

auto AssetLibrary::loadTexture(AssetId id) -> AssetRef<assets::Texture>
{
    return m_impl->loadTexture(id);
}

auto AssetLibrary::loadMesh(AssetId id) -> AssetRef<assets::StaticMesh>
{
    return m_impl->loadMesh(id);
}

auto AssetLibrary::loadMaterial(AssetId id) -> AssetRef<assets::Material>
{
    return m_impl->loadMaterial(id);
}

auto AssetLibrary::loadEnvironment(AssetId id) -> AssetRef<assets::Environment>
{
    return m_impl->loadEnvironment(id);
}

auto AssetLibrary::loadModel(AssetId id) -> AssetRef<assets::Model>
{
    return m_impl->loadModel(id);
}

auto AssetLibrary::addTexture(std::shared_ptr<assets::Texture> asset) -> AssetRef<assets::Texture>
{
    return m_impl->textures().fill(AssetId::generate(), std::move(asset));
}

auto AssetLibrary::addMesh(std::shared_ptr<assets::StaticMesh> asset) -> AssetRef<assets::StaticMesh>
{
    return m_impl->meshes().fill(AssetId::generate(), std::move(asset));
}

auto AssetLibrary::addMaterial(std::shared_ptr<assets::Material> asset) -> AssetRef<assets::Material>
{
    return m_impl->materials().fill(AssetId::generate(), std::move(asset));
}

auto AssetLibrary::addEnvironment(std::shared_ptr<assets::Environment> asset) -> AssetRef<assets::Environment>
{
    return m_impl->environments().fill(AssetId::generate(), std::move(asset));
}

auto AssetLibrary::addModel(std::shared_ptr<assets::Model> asset) -> AssetRef<assets::Model>
{
    return m_impl->models().fill(AssetId::generate(), std::move(asset));
}
}
