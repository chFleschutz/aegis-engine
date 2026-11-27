module aegis.asset_library;

import std;
import aegis.audio;
import aegis.core;
import aegis.paths;
import aegis.reflect;
import aegis.render_passes;
import aegis.serialize;
import aegis.world;

namespace aegis
{
using assets::AssetId;
using assets::AssetRef;
using assets::AssetSlot;
using assets::AssetState;

namespace
{
auto readFile(const std::filesystem::path& path) -> std::optional<std::string>
{
    std::ifstream stream{ path, std::ios::binary };
    if (!stream)
        return std::nullopt;
    return std::string{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
}

}

class AssetLibrary::Impl
{
public:
    // What a loader returns: the asset, or why it failed.
    template<class T>
    using Loaded = std::expected<std::shared_ptr<T>, std::string>;

    Impl(rhi::Device& device, std::uint32_t framesInFlight, assets::DefaultAssets defaults, render_graph::Ibl ibl,
        assets::AssetDatabase database, assets::ImportCache importCache) :
        m_device{ &device },
        m_framesInFlight{ framesInFlight },
        m_defaults{ std::move(defaults) },
        m_ibl{ std::move(ibl) },
        m_database{ std::move(database) },
        m_importCache{ std::move(importCache) }
    {
    }

    // The loaders keep 'this'
    Impl(const Impl&) = delete;
    auto operator=(const Impl&) -> Impl& = delete;

    [[nodiscard]] auto defaults() const -> const assets::DefaultAssets& { return m_defaults; }
    [[nodiscard]] auto database() const -> const assets::AssetDatabase& { return m_database; }
    [[nodiscard]] auto database() -> assets::AssetDatabase& { return m_database; }
    [[nodiscard]] auto rescan() -> std::expected<assets::SourceChanges, assets::Error> { return m_database.rescan(); }

    auto reload(const assets::SourceChanges& changes) -> ReloadResult
    {
        ReloadResult result;
        // A texture's descriptor is rewritten in place, and no frame may still draw what is swapped out: the device
        // idles first, once, if anything loaded is affected (a new file or a saved scene is not)
        if (!affectsLoaded(changes))
            return result;
        m_device->waitIdle();

        const auto reloadId = [&](AssetId id, bool log) {
            const auto types = m_cache.reload(id, log);
            if (types.empty())
                return;
            result.reloaded.push_back(id);
            result.resync = result.resync || std::ranges::any_of(types, [](std::string_view type) {
                return type == assets::StaticMesh::assetType || type == assets::Material::assetType || type == assets::Model::assetType;
            });
        };
        for (const auto* files : { &changes.changed, &changes.added, &changes.removed })
        {
            for (const auto& file : *files)
                reloadFile(file, result, reloadId);
        }
        // The IDs that left the database (a removed material file's, say): their loads now fail, giving the fallback
        for (const auto id : changes.gone)
            reloadId(id, true);
        return result;
    }
    [[nodiscard]] auto importCache() const -> const assets::ImportCache& { return m_importCache; }
    [[nodiscard]] auto cache() -> AssetCache& { return m_cache; }

    // Registers the asset loaders of the engine's asset types.
    [[nodiscard]] auto registerLoaders() -> std::expected<void, AssetLibraryError>
    {
        const auto registered = std::array{
            // A reloaded texture keeps its object and descriptors, which materials hold
            m_cache.registerLoader<assets::Texture>({
                .load = [this](AssetId id) { return loadTexture(id); },
                .fallback = [this](AssetId id) { return id != assets::engine::checker ? m_cache.load<assets::Texture>(assets::engine::checker).shared() : nullptr; },
                .update = [this](assets::Texture& loaded, std::shared_ptr<assets::Texture> fresh) { return updateTexture(loaded, std::move(fresh)); },
            }),
            m_cache.registerLoader<assets::StaticMesh>({
                .load = [this](AssetId id) { return loadPart<assets::StaticMesh>(id, "mesh"); },
                .fallback = [this](AssetId) { return cubeMesh(); },
            }),
            m_cache.registerLoader<assets::Material>({
                .load = [this](AssetId id) { return loadMaterial(id); },
                .fallback = [this](AssetId) { return fallbackMaterial(); },
            }),
            m_cache.registerLoader<assets::Environment>({
                .load = [this](AssetId id) { return loadEnvironment(id); },
                .fallback = [this](AssetId) { return blackEnvironment(); },
            }),
            m_cache.registerLoader<assets::Model>({
                .load = [this](AssetId id) { return loadModel(id); },
                .fallback = [this](AssetId id) { return id != assets::engine::cube ? m_cache.load<assets::Model>(assets::engine::cube).shared() : nullptr; },
            }),
            m_cache.registerLoader<assets::AudioClip>({
                .load = [this](AssetId id) { return loadAudioClip(id); },
                .fallback = {}, // A missing clip is silent
            }),
            // A failure of a scene file is not logged: instantiating an instance of it returns it. One of a model
            // file's scene was logged by its model.
            m_cache.registerLoader<serialize::Scene>({
                .load = [this](AssetId id) { return loadSceneAsset(id); },
                .fallback = [this](AssetId id) { return sceneFallback(id); },
                .ready = [this] { return m_types != nullptr; },
                .logFailures = false,
            }),
        };
        for (const auto& result : registered)
        {
            if (!result)
                return std::unexpected{ result.error() };
        }
        return {};
    }

    // Reads the scene file 'id', checked against 'types'. A model file's scene is the one loadScene keeps.
    auto readScene(AssetId id, const reflect::TypeRegistry& types) -> std::expected<serialize::Scene, AssetLibraryError>
    {
        const auto* entry = m_database.find(id);
        if (entry && isModelFile(*entry))
        {
            const auto scene = loadScene(id, types);
            if (!scene)
                return makeError(AssetLibraryErrorCode::ReadFailed, scene.slot()->message);
            return *scene;
        }
        return readSceneFile(id, types);
    }

    // The scene file or model file 'id' as an asset, read against 'types' on first use (the scene loader reads them
    // while this runs).
    auto loadScene(AssetId id, const reflect::TypeRegistry& types) -> AssetRef<serialize::Scene>
    {
        const auto* outer = std::exchange(m_types, &types);
        auto scene = m_cache.load<serialize::Scene>(id);
        m_types = outer;
        return scene;
    }

    // Loads the assets a component read from a scene file names by ID only (a reference without a slot).
    auto resolve(const reflect::TypeInfo& type, void* component, const reflect::TypeRegistry& types) -> void
    {
        if (type.type == typeid(serialize::SceneInstance))
        {
            auto& instance = *static_cast<serialize::SceneInstance*>(component);
            if (!instance.scene.slot())
                instance.scene = loadScene(instance.scene.id(), types);
            return;
        }
        if (type.type == typeid(MeshRenderer))
        {
            auto& renderer = *static_cast<MeshRenderer*>(component);
            if (renderer.mesh.id().valid() && !renderer.mesh.slot())
                renderer.mesh = m_cache.load<assets::StaticMesh>(renderer.mesh.id());
            if (renderer.material.id().valid() && !renderer.material.slot())
                renderer.material = m_cache.load<assets::Material>(renderer.material.id());
        }
        else if (type.type == typeid(Sky))
        {
            auto& sky = *static_cast<Sky*>(component);
            if (!sky.environment.id().valid())
                sky.environment = defaultEnvironment();
            else if (!sky.environment.slot())
                sky.environment = m_cache.load<assets::Environment>(sky.environment.id());
        }
        else if (type.type == typeid(audio::AudioSource))
        {
            auto& source = *static_cast<audio::AudioSource*>(component);
            if (source.clip.id().valid() && !source.clip.slot())
                source.clip = m_cache.load<assets::AudioClip>(source.clip.id());
        }
    }

    auto createMaterial(std::shared_ptr<const assets::MaterialTemplate> materialTemplate) -> std::expected<AssetRef<assets::Material>, AssetLibraryError>
    {
        if (!materialTemplate)
            materialTemplate = m_defaults.pbrTemplate;
        auto material = assets::Material::create(*m_device, std::move(materialTemplate), m_framesInFlight);
        if (!material)
            return makeError(AssetLibraryErrorCode::CreateFailed, material.error(), "cannot create a material");
        return m_cache.fill<assets::Material>(AssetId::generate(), std::move(*material));
    }

    auto copyMaterial(const AssetRef<assets::Material>& source) -> std::expected<AssetRef<assets::Material>, AssetLibraryError>
    {
        if (!source)
            return createMaterial(nullptr);
        auto material = source->copy();
        if (!material)
            return makeError(AssetLibraryErrorCode::CreateFailed, material.error(), "cannot copy a material");
        return m_cache.fill<assets::Material>(AssetId::generate(), std::move(*material));
    }

    auto defaultEnvironment() -> AssetRef<assets::Environment>
    {
        if (!m_defaultEnvironment.valid())
        {
            m_defaultEnvironment = AssetId::generate();
            m_cache.fill<assets::Environment>(m_defaultEnvironment, blackEnvironment());
        }
        return *m_cache.find<assets::Environment>(m_defaultEnvironment);
    }

    auto applyMaterialFile(AssetId id, const serialize::MaterialFile& file) -> std::expected<void, AssetLibraryError>
    {
        const auto* found = m_database.find(id);
        if (!found)
            return makeError(AssetLibraryErrorCode::NotFound, std::format("no asset has the ID {}", id));
        // A copy: its textures may load a model, which moves the database's entries
        const std::optional<assets::AssetEntry> entry{ *found };
        if (entry->type != "Material" || !entry->key.empty())
            return makeError(AssetLibraryErrorCode::WrongType, std::format("'{}' is not a material file", entry->path));

        auto material = m_cache.load<assets::Material>(id);
        if (material.state() != AssetState::Ready)
            return makeError(AssetLibraryErrorCode::ReadFailed, std::format("'{}' did not load: {}", entry->path, material.slot()->message));
        auto materialTemplate = fileTemplate(file);
        if (!materialTemplate)
            return makeError(AssetLibraryErrorCode::InvalidValue, std::format("'{}': {}", entry->path, materialTemplate.error()));
        if (*materialTemplate != material->materialTemplate())
            return makeError(AssetLibraryErrorCode::InvalidValue, std::format("'{}': its template cannot change while it is loaded", entry->path));

        // Every value is converted before any is set, so a bad one changes nothing
        std::unordered_map<std::string, assets::MaterialValue> overrides;
        for (const auto& value : file.parameters.items)
        {
            auto converted = convertParameter(**materialTemplate, value);
            if (!converted)
                return makeError(AssetLibraryErrorCode::InvalidValue, std::format("'{}': {}", entry->path, converted.error()));
            overrides.insert_or_assign(value.key, std::move(*converted));
        }
        if (auto set = material.shared()->setOverrides(std::move(overrides)); !set)
            return makeError(AssetLibraryErrorCode::InvalidValue, set.error(), std::format("cannot set the parameters of '{}'", entry->path));
        return {};
    }

private:
    // Whether a reload of 'changes' would touch a loaded asset: one of the files' IDs, or a gone ID, has a slot.
    auto affectsLoaded(const assets::SourceChanges& changes) const -> bool
    {
        if (std::ranges::any_of(changes.gone, [this](AssetId id) { return m_cache.loaded(id); }))
            return true;
        std::set<std::pair<std::size_t, std::string_view>> files;
        for (const auto* list : { &changes.changed, &changes.added, &changes.removed })
        {
            for (const auto& file : *list)
                files.emplace(file.root, file.path);
        }
        return std::ranges::any_of(m_database.entries(), [&](const assets::AssetEntry& entry) {
            return files.contains({ entry.root, entry.path }) && m_cache.loaded(entry.id);
        });
    }

    // Reloads the loaded assets of one source file: its main asset, and for a model file the parts the new import
    // fills, then the loaded parts it did not fill, which fall back.
    template<class ReloadId>
    auto reloadFile(const assets::SourceFile& file, ReloadResult& result, const ReloadId& reloadId) -> void
    {
        const auto main = m_database.find(file.root, file.path);
        if (!main)
            return; // A removed native file or sidecar: its IDs are in SourceChanges::gone
        std::vector<AssetId> parts;
        for (const auto& entry : m_database.entries())
        {
            if (entry.root == file.root && entry.path == file.path && !entry.key.empty())
                parts.push_back(entry.id);
        }
        if (parts.empty())
        {
            reloadId(*main, true);
            return;
        }

        std::unordered_set<AssetId> filled;
        m_filledParts = &filled;
        reloadId(*main, true);
        // The parts the model's load filled have their new contents (new parts join the database). The loaded parts it
        // did not fill fall back, quietly when the whole model failed, which was logged.
        const auto model = m_cache.find<assets::Model>(*main);
        const bool modelFailed = model && model->state() == AssetState::Failed;
        for (const auto part : parts)
        {
            if (filled.contains(part))
            {
                result.reloaded.push_back(part);
                result.resync = true;
            }
            else
            {
                reloadId(part, !modelFailed);
            }
        }
        m_filledParts = nullptr;
    }

    // A texture's update hook: it takes the image of 'fresh', or for a failed reload of a new checker (the fallback
    // texture is shared).
    auto updateTexture(assets::Texture& loaded, std::shared_ptr<assets::Texture> fresh) -> std::expected<void, std::string>
    {
        if (!fresh)
        {
            auto checker = loadTexture(assets::engine::checker);
            if (!checker)
                return std::unexpected{ std::format("cannot load the fallback: {}", checker.error()) };
            fresh = std::move(*checker);
        }
        if (auto adopted = loaded.adopt(*fresh); !adopted)
            return std::unexpected{ assets::describe(adopted.error()) };
        return {};
    }

    // The loaders of the engine's types. A part of a model file (a mesh, a material or an embedded texture) loads its
    // model, which fills the slots of all its parts.

    auto loadTexture(AssetId id) -> Loaded<assets::Texture>
    {
        const auto* entry = m_database.find(id);
        if (!entry)
            return std::unexpected{ "no asset has this ID" };
        if (!entry->key.empty())
            return loadPart<assets::Texture>(id, "texture");
        if (assets::importerFor(entry->path) != "texture")
            return std::unexpected{ std::format("'{}' is not a texture file", entry->path) };

        auto loaded = m_importCache.loadTextureFile(m_database.absolutePath(*entry), { .name = entry->path, .srgb = assets::textureSrgb(entry->settings, true) });
        if (!loaded)
            return std::unexpected{ assets::describe(loaded.error()) };
        auto texture = assets::Texture::create(*m_device, loaded->texture, entry->path);
        if (!texture)
            return std::unexpected{ assets::describe(texture.error()) };
        return std::move(*texture);
    }

    // A part of a model file: its slot, filled by loading the model. While a reload runs, only the parts its new load
    // filled are there.
    template<class T>
    auto loadPart(AssetId id, std::string_view what) -> Loaded<T>
    {
        const auto* found = m_database.find(id);
        if (!found)
            return std::unexpected{ "no asset has this ID" };
        const auto entry = *found; // Loading the model may move the database's entries
        if (entry.key.empty())
            return std::unexpected{ std::format("it is a file, not a {}", what) };
        loadOwner(entry);
        if (auto ref = m_cache.find<T>(id); ref && (!m_filledParts || m_filledParts->contains(id)))
            return ref->shared();
        return std::unexpected{ std::format("'{}' has no {} '{}'", entry.path, what, entry.key) };
    }

    auto loadMaterial(AssetId id) -> Loaded<assets::Material>
    {
        const auto* entry = m_database.find(id);
        if (entry && entry->key.empty() && entry->type == "Material")
            return loadMaterialFile(*entry); // A copy: its textures may load a model, which moves the database's entries
        return loadPart<assets::Material>(id, "material");
    }

    // The file's bytes come from the import cache unchanged and are decoded into memory on every load.
    auto loadAudioClip(AssetId id) -> Loaded<assets::AudioClip>
    {
        const auto* entry = m_database.find(id);
        if (!entry)
            return std::unexpected{ "no asset has this ID" };
        if (!entry->key.empty() || assets::importerFor(entry->path) != "audio")
            return std::unexpected{ std::format("'{}' is not an audio file", entry->path) };

        auto loaded = m_importCache.loadAudioFile(m_database.absolutePath(*entry));
        if (!loaded)
            return std::unexpected{ assets::describe(loaded.error()) };
        auto clip = audio::decodeClip(loaded->bytes, entry->path);
        if (!clip)
            return std::unexpected{ audio::describe(clip.error()) };
        return std::make_shared<assets::AudioClip>(std::move(*clip));
    }

    auto loadEnvironment(AssetId id) -> Loaded<assets::Environment>
    {
        const auto* entry = m_database.find(id);
        if (!entry)
            return std::unexpected{ "no asset has this ID" };
        if (!entry->key.empty() || assets::importerFor(entry->path) != "environment")
            return std::unexpected{ std::format("'{}' is not an environment file", entry->path) };

        // The equirectangular map comes from the import cache; the cube and its maps are made on the GPU each load.
        auto equirect = m_importCache.loadTextureFile(m_database.absolutePath(*entry), { .name = entry->path, .environment = true });
        if (!equirect)
            return std::unexpected{ assets::describe(equirect.error()) };
        auto texture = assets::Texture::create(*m_device, equirect->texture, entry->path, { .addressMode = rhi::SamplerAddressMode::ClampToEdge });
        if (!texture)
            return std::unexpected{ assets::describe(texture.error()) };
        auto cube = m_ibl.cubeFromEquirectangular(**texture);
        if (!cube)
            return std::unexpected{ render_graph::describe(cube.error()) };
        auto environment = m_ibl.environmentFrom(std::move(*cube));
        if (!environment)
            return std::unexpected{ render_graph::describe(environment.error()) };
        return std::move(*environment);
    }

    auto loadModel(AssetId id) -> Loaded<assets::Model>
    {
        const auto* found = m_database.find(id);
        if (!found)
            return std::unexpected{ "no asset has this ID" };
        const auto entry = *found; // Registering the parts moves the database's entries
        if (!isModelFile(entry))
            return std::unexpected{ std::format("'{}' is not a model file", entry.path) };

        auto imported = m_importCache.loadModel(m_database.absolutePath(entry), entry.settings);
        if (!imported)
            return std::unexpected{ assets::describe(imported.error()) };
        // External images are textures of their own, loaded and shared by their own IDs; embedded ones are parts,
        // with their settings in the model's sidecar.
        assets::resolveExternalImages(m_database, entry, imported->model);
        auto sidecar = readModelSidecar(entry);
        assets::SubAssetKeys keys{ sidecar.sidecar.settings.strictNames };
        auto imageKeys = assets::embeddedImageKeys(imported->model, keys);
        if (!imageKeys)
            return std::unexpected{ assets::describe(imageKeys.error()) };
        assets::applyEmbeddedImageSettings(sidecar.sidecar, *imageKeys, imported->model);
        const auto externalTexture = [this](const assets::ModelImage& image) {
            return image.texture.valid() ? m_cache.load<assets::Texture>(image.texture).shared() : nullptr;
        };
        auto model = assets::Loader::upload(
            { .device = *m_device, .defaults = m_defaults, .framesInFlight = m_framesInFlight, .externalTexture = externalTexture, .importCache = &m_importCache },
            imported->model);
        if (!model)
            return std::unexpected{ assets::describe(model.error()) };

        auto shared = std::make_shared<assets::Model>(std::move(*model));
        if (auto registered = registerParts(entry, std::move(sidecar), keys, *imageKeys, *shared); !registered)
            return std::unexpected{ assets::describe(registered.error()) };
        return shared;
    }

    // A scene file, or a model file's scene, read against the types loadScene was given.
    auto loadSceneAsset(AssetId id) -> Loaded<serialize::Scene>
    {
        if (const auto* entry = m_database.find(id); entry && isModelFile(*entry))
            return loadModelScene(id, *m_types);
        auto scene = readSceneFile(id, *m_types);
        if (!scene)
            return std::unexpected{ describe(scene.error()) };
        return std::make_shared<serialize::Scene>(std::move(*scene));
    }

    // A model file whose model failed (logged by its loader) gives the engine cube's scene.
    auto sceneFallback(AssetId id) -> std::shared_ptr<serialize::Scene>
    {
        const auto model = m_cache.find<assets::Model>(id);
        if (!model || model->state() != AssetState::Failed || id == assets::engine::cube)
            return nullptr;
        return loadScene(assets::engine::cube, *m_types).shared();
    }

    // Reads the scene file 'id', checked against 'types'.
    auto readSceneFile(AssetId id, const reflect::TypeRegistry& types) -> std::expected<serialize::Scene, AssetLibraryError>
    {
        const auto* entry = m_database.find(id);
        if (!entry)
            return makeError(AssetLibraryErrorCode::NotFound, std::format("no asset has the ID {}", id));
        if (entry->type != "Scene")
            return makeError(AssetLibraryErrorCode::WrongType, std::format("{} ('{}') is not a scene file", id, entry->path));
        auto text = readFile(m_database.absolutePath(*entry));
        if (!text)
            return makeError(AssetLibraryErrorCode::ReadFailed, std::format("cannot read '{}'", entry->path));
        auto scene = serialize::parseScene(*text, types);
        if (!scene)
            return makeError(AssetLibraryErrorCode::ReadFailed, scene.error(), std::format("cannot parse '{}'", entry->path));
        return std::move(*scene);
    }

    // The main asset of a .gltf, .glb or .obj file, which loads as a Model and as a Scene.
    static auto isModelFile(const assets::AssetEntry& entry) -> bool
    {
        return entry.key.empty() && assets::importerFor(entry.path) == "model";
    }

    // The scene of the model file 'id' (see serialize::modelScene), with the local IDs of its entities from the
    // file's sidecar: new entities get new IDs, recorded there.
    auto loadModelScene(AssetId id, const reflect::TypeRegistry& types) -> Loaded<serialize::Scene>
    {
        const auto model = m_cache.load<assets::Model>(id); // Adds the model's parts to the database, which moves its entries
        if (model.state() == AssetState::Failed)
            return std::unexpected{ model.slot()->message };

        const auto entry = *m_database.find(id);
        auto [sidecar, keepReason] = readModelSidecar(entry);
        const auto known = sidecar.subAssets.size();
        auto scene = serialize::modelScene(*model, id.value, types, [&sidecar](const std::string& key) { return sidecar.subAssetId(key).value; }, sidecar.settings.strictNames);
        if (!scene)
            return std::unexpected{ std::format("'{}': {}", entry.path, serialize::describe(scene.error())) };
        saveModelSidecar(entry, sidecar, sidecar.subAssets.size() - known, keepReason);
        return std::make_shared<serialize::Scene>(std::move(*scene));
    }

    // A material file: the template's parameters with the file's values. A bad file, an unknown template or a
    // value the template's parameter cannot take fails the whole material (the checker fallback).
    auto loadMaterialFile(assets::AssetEntry entry) -> Loaded<assets::Material>
    {
        auto text = readFile(m_database.absolutePath(entry));
        if (!text)
            return std::unexpected{ std::format("cannot read '{}'", entry.path) };
        auto file = serialize::parseMaterial(*text);
        if (!file)
            return std::unexpected{ std::format("'{}' {}", entry.path, serialize::describe(file.error())) };

        auto materialTemplate = fileTemplate(*file);
        if (!materialTemplate)
            return std::unexpected{ std::format("'{}': {}", entry.path, materialTemplate.error()) };
        auto material = assets::Material::create(*m_device, *materialTemplate, m_framesInFlight);
        if (!material)
            return std::unexpected{ assets::describe(material.error()) };

        for (const auto& value : file->parameters.items)
        {
            auto converted = convertParameter(**materialTemplate, value);
            if (!converted)
                return std::unexpected{ std::format("'{}' line {}: {}", entry.path, value.line, converted.error()) };
            if (auto set = (*material)->setParameter(value.key, std::move(*converted)); !set)
                return std::unexpected{ std::format("'{}' line {}: {}", entry.path, value.line, assets::describe(set.error())) };
        }
        return std::move(*material);
    }

    // The template a material file names. Only one exists until custom shaders: the engine content's PBR template,
    // built in code.
    auto fileTemplate(const serialize::MaterialFile& file) const -> std::expected<std::shared_ptr<const assets::MaterialTemplate>, std::string>
    {
        if (AssetId{ file.templateId } != assets::engine::pbrTemplate)
            return std::unexpected{ std::format("{} is not a material template", AssetId{ file.templateId }) };
        return m_defaults.pbrTemplate;
    }

    // A material file's value of the parameter 'value.key', read as the kind of the parameter's default. A texture
    // loads by its ID (the checker if it fails).
    auto convertParameter(const assets::MaterialTemplate& materialTemplate, const reflect::Node& value) -> std::expected<assets::MaterialValue, std::string>
    {
        const auto* parameter = materialTemplate.parameter(value.key);
        if (!parameter)
            return std::unexpected{ std::format("the template has no parameter '{}'", value.key) };

        return std::visit(
            [&](const auto& defaultValue) -> std::expected<assets::MaterialValue, std::string> {
                using T = std::decay_t<decltype(defaultValue)>;
                if constexpr (std::same_as<T, std::shared_ptr<assets::Texture>>)
                {
                    auto textureId = reflect::fromNode(reflect::FieldKind::AssetRef, value);
                    if (!textureId)
                        return std::unexpected{ std::format("'{}': {}", value.key, reflect::describe(textureId.error())) };
                    return m_cache.load<assets::Texture>(AssetId{ std::get<std::uint64_t>(*textureId) }).shared();
                }
                else
                {
                    using Conversion = reflect::FieldConversion<T>;
                    auto read = reflect::fromNode(Conversion::kind, value).and_then(Conversion::fromValue);
                    if (!read)
                        return std::unexpected{ std::format("'{}': {}", value.key, reflect::describe(read.error())) };
                    return *read;
                }
            },
            parameter->defaultValue);
    }

    // Loads the model file that 'part' belongs to, which fills the slots of all its parts.
    auto loadOwner(const assets::AssetEntry& part) -> void
    {
        if (const auto owner = m_database.find(part.root, part.path))
            m_cache.load<assets::Model>(*owner);
    }

    // Every missing material shares one fallback, like the other types share their engine content.
    auto fallbackMaterial() -> std::shared_ptr<assets::Material>
    {
        if (!m_fallbackMaterial)
        {
            if (auto material = m_defaults.createPbrMaterial(*m_device, m_framesInFlight))
            {
                m_fallbackMaterial = std::move(*material);
                if (const auto checker = m_cache.load<assets::Texture>(assets::engine::checker).shared())
                    (void)m_fallbackMaterial->setParameter(assets::pbr::albedoMap, checker);
            }
        }
        return m_fallbackMaterial;
    }

    // The default cube's mesh, or none if the engine content is missing it.
    auto cubeMesh() -> std::shared_ptr<assets::StaticMesh>
    {
        const auto cube = m_cache.load<assets::Model>(assets::engine::cube);
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

    // The sidecar of a model file, and why writing it back would lose something (empty if it would not).
    struct ModelSidecar
    {
        assets::Sidecar sidecar;
        std::string keepReason;
    };

    // Reads the sidecar registerParts adds new parts to. It is rewritten only when it is in the exact form
    // writeSidecar gives (line endings aside): comments, keys this version does not know and hand edits are
    // never lost, and an unreadable sidecar is never replaced by a fresh one.
    auto readModelSidecar(const assets::AssetEntry& entry) const -> ModelSidecar
    {
        ModelSidecar result{ .sidecar = { .id = entry.id, .importer = "model" }, .keepReason = {} };
        if (m_database.roots()[entry.root].readOnly)
            result.keepReason = "its folder is read-only";

        auto text = readFile(assets::sidecarPath(m_database.absolutePath(entry)));
        if (!text)
            return result;
        auto sidecar = assets::parseSidecar(*text);
        if (!sidecar)
        {
            result.keepReason = std::format("it does not parse: {}", assets::describe(sidecar.error()));
            return result;
        }
        std::erase(*text, '\r');
        if (result.keepReason.empty() && assets::writeSidecar(*sidecar) != *text)
            result.keepReason = "it differs from the written form (comments, unknown keys or hand edits) that rewriting would lose";
        result.sidecar = std::move(*sidecar);
        return result;
    }

    // Writes back the sidecar of a model file that got 'added' new IDs (of parts or entities), unless 'keepReason'
    // says why it must stay as it is. The database then reads it again, so a rescan does not take the library's own
    // write for a change of the source (which moves the database's entries: 'entry' must not be one of them).
    auto saveModelSidecar(const assets::AssetEntry& entry, const assets::Sidecar& sidecar, std::size_t added, const std::string& keepReason) -> void
    {
        if (added == 0)
            return;
        const auto path = assets::sidecarPath(m_database.absolutePath(entry));
        if (!keepReason.empty())
        {
            log::warn("'{}' lacks {} ID(s) and is not rewritten ({}); they are new every run", path.generic_string(), added, keepReason);
            return;
        }
        {
            std::ofstream out{ path, std::ios::binary };
            if (!out)
            {
                log::warn("Cannot write the sidecar '{}'; the parts and entities of this model get new IDs next run", path.generic_string());
                return;
            }
            out << assets::writeSidecar(sidecar);
        }
        if (auto read = m_database.addFile(entry.root, entry.path); !read)
            log::warn("The asset database does not take the rewritten sidecar '{}': {}", path.generic_string(), assets::describe(read.error()));
    }

    // Gives the parts of a freshly loaded model their IDs from the file's sidecar (new parts get new IDs,
    // recorded in it), fills their slots, and points the model's primitives at those slots. 'keys' already has
    // the embedded images' keys, 'imageKeys' (see assets::embeddedImageKeys).
    auto registerParts(const assets::AssetEntry& entry, ModelSidecar modelSidecar, assets::SubAssetKeys& keys,
        std::span<const std::string> imageKeys, assets::Model& model) -> std::expected<void, assets::Error>
    {
        auto& [sidecar, keepReason] = modelSidecar;
        const auto known = sidecar.subAssets.size();

        std::vector<std::pair<std::string, AssetId>> parts;
        const auto addPart = [&](const std::string& key) {
            const auto id = sidecar.subAssetId(key);
            parts.emplace_back(key, id);
            return id;
        };

        std::vector<AssetId> textureIds, materialIds;
        for (const auto& key : imageKeys) // Model::textures holds the embedded images in this order
        {
            if (!key.empty())
                textureIds.push_back(addPart(key));
        }
        std::unordered_map<const assets::Material*, AssetRef<assets::Material>> materialRefs;
        for (std::size_t i = 0; i < model.materials.size(); ++i)
        {
            auto key = keys.name("Material", model.materials[i].name, i);
            if (!key)
                return std::unexpected{ std::move(key.error()) };
            materialIds.push_back(addPart(*key));
            materialRefs.emplace(model.materials[i].material.get(), AssetRef<assets::Material>{});
        }
        std::vector<std::vector<AssetId>> meshIds;
        AssetId defaultMaterialId; // Of the load's default material, if a primitive has no material of the file
        for (std::size_t i = 0; i < model.meshes.size(); ++i)
        {
            auto key = keys.name("StaticMesh", model.meshes[i].name, i);
            if (!key)
                return std::unexpected{ std::move(key.error()) };
            auto& ids = meshIds.emplace_back();
            for (std::size_t j = 0; j < model.meshes[i].primitives.size(); ++j)
            {
                ids.push_back(addPart(assets::SubAssetKeys::withIndex(*key, j)));
                if (!defaultMaterialId.valid() && !materialRefs.contains(model.meshes[i].primitives[j].material.get()))
                    defaultMaterialId = addPart(std::string{ assets::SubAssetKeys::defaultMaterial });
            }
        }

        saveModelSidecar(entry, sidecar, sidecar.subAssets.size() - known, keepReason);
        if (auto added = m_database.addSubAssets(entry.root, entry.path, parts); !added)
            return std::unexpected{ std::move(added.error()) };

        // While a reload runs, it learns which parts the new load has
        if (m_filledParts)
        {
            for (const auto& [key, id] : parts)
                m_filledParts->insert(id);
        }
        for (std::size_t i = 0; i < model.textures.size(); ++i)
            m_cache.fill<assets::Texture>(textureIds[i], model.textures[i].texture);
        for (std::size_t i = 0; i < model.materials.size(); ++i)
            materialRefs[model.materials[i].material.get()] = m_cache.fill<assets::Material>(materialIds[i], model.materials[i].material);

        for (std::size_t i = 0; i < model.meshes.size(); ++i)
        {
            for (std::size_t j = 0; j < model.meshes[i].primitives.size(); ++j)
            {
                auto& primitive = model.meshes[i].primitives[j];
                primitive.mesh = m_cache.fill<assets::StaticMesh>(meshIds[i][j], primitive.mesh.shared());

                // Primitives without a material share the load's one default material (see Loader).
                auto [it, inserted] = materialRefs.try_emplace(primitive.material.get());
                if (inserted)
                    it->second = m_cache.fill<assets::Material>(defaultMaterialId, primitive.material.shared());
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
    assets::ImportCache m_importCache;
    AssetId m_defaultEnvironment;
    std::shared_ptr<assets::Material> m_fallbackMaterial;
    const reflect::TypeRegistry* m_types{ nullptr };       // While loadScene runs: the types its scenes are read against
    std::unordered_set<AssetId>* m_filledParts{ nullptr }; // While reload reloads a model file: the parts its load filled
    AssetCache m_cache;
};

auto describe(const AssetLibraryError& error) -> std::string
{
    std::string text{ errorCodeName(error.code) };
    if (!error.message.empty())
        text += std::format(": {}", error.message);
    if (error.assets)
        text += std::format(" (caused by {})", assets::describe(*error.assets));
    else if (error.serialize)
        text += std::format(" (caused by {})", serialize::describe(*error.serialize));
    return text;
}

auto AssetLibrary::Sources::standard() -> Sources
{
    return {
        .project = paths::project(),
        .engineContent = paths::content(),
        .indexFile = paths::cache() / "index",
        .importCache = paths::cache() / "imported",
    };
}

AssetLibrary::AssetLibrary(std::unique_ptr<Impl> impl) :
    m_impl{ std::move(impl) },
    m_cache{ &m_impl->cache() },
    m_defaults{ &m_impl->defaults() },
    m_database{ &m_impl->database() },
    m_importCache{ &m_impl->importCache() }
{
}

AssetLibrary::AssetLibrary(AssetLibrary&&) noexcept = default;
auto AssetLibrary::operator=(AssetLibrary&&) noexcept -> AssetLibrary& = default;
AssetLibrary::~AssetLibrary() = default;

auto loadEngineTemplates(const std::filesystem::path& engineContent, const std::filesystem::path& shaders)
    -> std::expected<assets::EngineTemplates, AssetLibraryError>
{
    const auto load = [&](std::string_view name) -> std::expected<assets::TemplateSource, AssetLibraryError> {
        const auto templatePath = engineContent / std::format("{}.template", name);
        auto text = readFile(templatePath);
        if (!text)
            return makeError(AssetLibraryErrorCode::ReadFailed, std::format("cannot read '{}'", templatePath.string()));
        auto file = serialize::parseTemplate(*text);
        if (!file)
            return makeError(AssetLibraryErrorCode::ReadFailed, file.error(), std::format("cannot parse '{}'", templatePath.string()));

        const auto layoutPath = shaders / "surfaces" / std::format("{}.layout.json", file->desc.surface);
        auto layout = readFile(layoutPath);
        if (!layout)
            return makeError(AssetLibraryErrorCode::ReadFailed, std::format("'{}': cannot read '{}', the layout of its surface shader", templatePath.string(), layoutPath.string()));
        auto block = serialize::parseSurfaceLayout(*layout);
        if (!block)
            return makeError(AssetLibraryErrorCode::ReadFailed, block.error(), std::format("cannot parse '{}'", layoutPath.string()));
        return assets::TemplateSource{ .name = templatePath.filename().string(), .desc = std::move(file->desc), .block = std::move(*block) };
    };

    auto pbr = load("pbr");
    if (!pbr)
        return std::unexpected{ std::move(pbr.error()) };
    auto unlit = load("unlit");
    if (!unlit)
        return std::unexpected{ std::move(unlit.error()) };
    return assets::EngineTemplates{ .pbr = std::move(*pbr), .unlit = std::move(*unlit) };
}

auto AssetLibrary::create(rhi::Device& device, std::uint32_t framesInFlight, const Sources& sources)
    -> std::expected<AssetLibrary, AssetLibraryError>
{
    auto database = assets::AssetDatabase::scan({
        .roots = { { paths::projectContent(sources.project) }, { sources.engineContent, true } },
        .indexFile = sources.indexFile,
        .createSidecars = true,
    });
    if (!database)
        return makeError(AssetLibraryErrorCode::ReadFailed, database.error(), "failed to scan the assets");

    auto templates = loadEngineTemplates(sources.engineContent, paths::shaders());
    if (!templates)
    {
        templates.error().message = std::format("failed to load the material templates: {}", templates.error().message);
        return std::unexpected{ std::move(templates.error()) };
    }
    auto defaults = assets::DefaultAssets::create(device, framesInFlight, *templates);
    if (!defaults)
        return makeError(AssetLibraryErrorCode::CreateFailed, defaults.error(), "failed to create the default assets");
    auto ibl = render_graph::Ibl::create(device);
    if (!ibl)
        return makeError(AssetLibraryErrorCode::CreateFailed, std::format("failed to create the IBL pipelines: {}", render_graph::describe(ibl.error())));

    // Entries unused for long, or past the size limit, are deleted once per start.
    assets::ImportCache importCache{ sources.importCache };
    (void)importCache.evict();

    auto impl = std::make_unique<Impl>(device, framesInFlight, std::move(*defaults), std::move(*ibl), std::move(*database), std::move(importCache));
    if (auto registered = impl->registerLoaders(); !registered)
        return std::unexpected{ std::move(registered.error()) };
    return AssetLibrary{ std::move(impl) };
}

auto AssetLibrary::rescan() -> std::expected<assets::SourceChanges, AssetLibraryError>
{
    auto scanned = m_impl->rescan();
    if (!scanned)
        return makeError(AssetLibraryErrorCode::ReadFailed, scanned.error(), "failed to rescan the assets");
    return std::move(*scanned);
}

auto AssetLibrary::reload(const assets::SourceChanges& changes) -> ReloadResult
{
    return m_impl->reload(changes);
}

auto AssetLibrary::find(std::string_view path, std::string_view key) const -> std::optional<AssetId>
{
    return m_database->find(0, path, key);
}

auto AssetLibrary::addFile(std::string_view view) -> std::expected<AssetId, AssetLibraryError>
{
    // A copy: 'view' may name an entry's path, which adding the file moves
    const std::string path{ view };
    auto& database = m_impl->database();
    if (auto added = database.addFile(0, path); !added)
        return makeError(AssetLibraryErrorCode::ReadFailed, added.error(), std::format("cannot add '{}'", path));
    const auto found = database.find(0, path);
    if (!found)
        return makeError(AssetLibraryErrorCode::NotFound, std::format("'{}' holds no asset", path));
    return *found;
}

auto AssetLibrary::id(std::string_view path, std::string_view key) const -> std::expected<AssetId, AssetLibraryError>
{
    const auto found = find(path, key);
    if (!found)
        return makeError(AssetLibraryErrorCode::NotFound, std::format("no asset '{}'{}{} in the project", path, key.empty() ? "" : " / ", key));
    return *found;
}

auto AssetLibrary::instantiateScene(world::World& world, const reflect::TypeRegistry& types, AssetId scene)
    -> std::expected<void, AssetLibraryError>
{
    auto read = m_impl->readScene(scene, types);
    if (!read)
        return std::unexpected{ std::move(read.error()) };
    const auto resolve = [this, &types](const reflect::TypeInfo& type, void* component) { m_impl->resolve(type, component, types); };
    if (auto created = serialize::instantiate(world.registry(), types, *read, resolve); !created)
        return makeError(AssetLibraryErrorCode::InstantiateFailed, created.error(), std::format("cannot instantiate scene {}", scene));
    return {};
}

void AssetLibrary::resolve(const reflect::TypeInfo& type, void* component, const reflect::TypeRegistry& types)
{
    m_impl->resolve(type, component, types);
}

auto AssetLibrary::instantiateScene(world::World& world, const reflect::TypeRegistry& types, AssetId scene, world::Entity parent,
    std::uint64_t holderId) -> std::expected<world::Entity, AssetLibraryError>
{
    const auto loaded = m_impl->loadScene(scene, types);
    if (!loaded)
        return makeError(AssetLibraryErrorCode::InstantiateFailed, std::format("cannot instantiate scene {}: {}", scene, loaded.slot()->message));
    const auto resolve = [this, &types](const reflect::TypeInfo& type, void* component) { m_impl->resolve(type, component, types); };
    auto holder = serialize::instantiate(world.registry(), types, loaded, parent, resolve, holderId);
    if (!holder)
        return makeError(AssetLibraryErrorCode::InstantiateFailed, holder.error(), std::format("cannot instantiate scene {}", scene));
    if (const auto* entry = m_database->find(scene))
        world.registry().get<Name>(*holder).name = std::filesystem::path{ entry->path }.stem().string();
    return *holder;
}

auto AssetLibrary::createMaterial(std::shared_ptr<const assets::MaterialTemplate> materialTemplate) -> std::expected<AssetRef<assets::Material>, AssetLibraryError>
{
    return m_impl->createMaterial(std::move(materialTemplate));
}

auto AssetLibrary::applyMaterialFile(assets::AssetId material, const serialize::MaterialFile& file) -> std::expected<void, AssetLibraryError>
{
    return m_impl->applyMaterialFile(material, file);
}

auto AssetLibrary::copyMaterial(const AssetRef<assets::Material>& material) -> std::expected<AssetRef<assets::Material>, AssetLibraryError>
{
    return m_impl->copyMaterial(material);
}

auto AssetLibrary::defaultEnvironment() -> AssetRef<assets::Environment>
{
    return m_impl->defaultEnvironment();
}

auto AssetLibrary::loadTexture(AssetId id) -> AssetRef<assets::Texture>
{
    return load<assets::Texture>(id);
}

auto AssetLibrary::loadMesh(AssetId id) -> AssetRef<assets::StaticMesh>
{
    return load<assets::StaticMesh>(id);
}

auto AssetLibrary::loadMaterial(AssetId id) -> AssetRef<assets::Material>
{
    return load<assets::Material>(id);
}

auto AssetLibrary::loadEnvironment(AssetId id) -> AssetRef<assets::Environment>
{
    return load<assets::Environment>(id);
}

auto AssetLibrary::loadModel(AssetId id) -> AssetRef<assets::Model>
{
    return load<assets::Model>(id);
}

auto AssetLibrary::loadAudioClip(AssetId id) -> AssetRef<assets::AudioClip>
{
    return load<assets::AudioClip>(id);
}
}
