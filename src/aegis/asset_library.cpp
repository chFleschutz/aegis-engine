module;

#include <cstdlib>

module aegis.asset_library;

import std;
import aegis.core;
import aegis.paths;
import aegis.reflect;
import aegis.render_passes;
import aegis.serialize;
import aegis.world.render_components;

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
        assets::AssetDatabase database, assets::ImportCache importCache) :
        m_device{ &device },
        m_framesInFlight{ framesInFlight },
        m_defaults{ std::move(defaults) },
        m_ibl{ std::move(ibl) },
        m_database{ std::move(database) },
        m_importCache{ std::move(importCache) }
    {
    }

    [[nodiscard]] auto defaults() const -> const assets::DefaultAssets& { return m_defaults; }
    [[nodiscard]] auto database() const -> const assets::AssetDatabase& { return m_database; }
    [[nodiscard]] auto database() -> assets::AssetDatabase& { return m_database; }
    [[nodiscard]] auto rescan() -> std::expected<void, assets::Error> { return m_database.rescan(); }
    [[nodiscard]] auto importCache() const -> const assets::ImportCache& { return m_importCache; }
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

        auto loaded = m_importCache.loadTextureFile(m_database.absolutePath(*entry), { .name = entry->path, .srgb = assets::textureSrgb(entry->settings, true) });
        if (!loaded)
            return failTexture(id, assets::describe(loaded.error()));
        auto texture = assets::Texture::create(*m_device, loaded->texture, entry->path);
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
        if (entry && entry->type == "Material")
            return loadMaterialFile(id, *entry);
        return failMaterial(id, entry ? "it is a file, not a material" : "no asset has this ID");
    }

    // Reads the scene file 'id', checked against 'types'. A model file's scene is the one loadScene keeps.
    auto readScene(AssetId id, const reflect::TypeRegistry& types) -> std::expected<serialize::Scene, std::string>
    {
        const auto* entry = m_database.find(id);
        if (entry && isModelFile(*entry))
        {
            const auto scene = loadScene(id, types);
            if (!scene)
                return std::unexpected{ scene.slot()->message };
            return *scene;
        }
        if (!entry || entry->type != "Scene")
            return std::unexpected{ std::format("{} is not a scene file", id) };
        auto text = readFile(m_database.absolutePath(*entry));
        if (!text)
            return std::unexpected{ std::format("cannot read '{}'", entry->path) };
        auto scene = serialize::parseScene(*text, types);
        if (!scene)
            return std::unexpected{ std::format("'{}' {}", entry->path, serialize::describe(scene.error())) };
        return std::move(*scene);
    }

    // The scene file or model file 'id' as an asset, read against 'types' on first use. A failure of a scene
    // file is not logged: instantiating an instance of it returns it.
    auto loadScene(AssetId id, const reflect::TypeRegistry& types) -> AssetRef<serialize::Scene>
    {
        if (auto ref = m_scenes.find(id))
            return *ref;
        if (const auto* entry = m_database.find(id); entry && isModelFile(*entry))
            return loadModelScene(id, types);
        auto scene = readScene(id, types);
        if (!scene)
        {
            const auto& slot = m_scenes.slot(id);
            slot->state = AssetState::Failed;
            slot->message = std::move(scene.error());
            return AssetRef<serialize::Scene>{ id, slot };
        }
        return m_scenes.fill(id, std::make_shared<serialize::Scene>(std::move(*scene)));
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
                renderer.mesh = loadMesh(renderer.mesh.id());
            if (renderer.material.id().valid() && !renderer.material.slot())
                renderer.material = loadMaterial(renderer.material.id());
        }
        else if (type.type == typeid(Sky))
        {
            auto& sky = *static_cast<Sky*>(component);
            if (!sky.environment.id().valid())
                sky.environment = defaultEnvironment();
            else if (!sky.environment.slot())
                sky.environment = loadEnvironment(sky.environment.id());
        }
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

        // The equirectangular map comes from the import cache; the cube and its maps are made on the GPU each load.
        auto equirect = m_importCache.loadTextureFile(m_database.absolutePath(*entry), { .name = entry->path, .environment = true });
        if (!equirect)
            return failEnvironment(id, assets::describe(equirect.error()));
        auto texture = assets::Texture::create(*m_device, equirect->texture, entry->path, { .addressMode = rhi::SamplerAddressMode::ClampToEdge });
        if (!texture)
            return failEnvironment(id, assets::describe(texture.error()));
        auto cube = m_ibl.cubeFromEquirectangular(**texture);
        if (!cube)
            return failEnvironment(id, render_graph::describe(cube.error()));
        auto environment = m_ibl.environmentFrom(std::move(*cube));
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
        if (!isModelFile(*entry))
            return failModel(id, std::format("'{}' is not a model file", entry->path));

        auto imported = m_importCache.loadModel(m_database.absolutePath(*entry), entry->settings);
        if (!imported)
            return failModel(id, assets::describe(imported.error()));
        // External images are textures of their own, loaded and shared by their own IDs; embedded ones are parts,
        // with their settings in the model's sidecar.
        assets::resolveExternalImages(m_database, *entry, imported->model);
        auto sidecar = readModelSidecar(*entry);
        assets::SubAssetKeys keys{ sidecar.sidecar.settings.strictNames };
        auto imageKeys = assets::embeddedImageKeys(imported->model, keys);
        if (!imageKeys)
            return failModel(id, assets::describe(imageKeys.error()));
        assets::applyEmbeddedImageSettings(sidecar.sidecar, *imageKeys, imported->model);
        const auto externalTexture = [this](const assets::ModelImage& image) {
            return image.texture.valid() ? loadTexture(image.texture).shared() : nullptr;
        };
        auto model = assets::Loader::upload(
            { .device = *m_device, .defaults = m_defaults, .framesInFlight = m_framesInFlight, .externalTexture = externalTexture, .importCache = &m_importCache },
            imported->model);
        if (!model)
            return failModel(id, assets::describe(model.error()));

        auto shared = std::make_shared<assets::Model>(std::move(*model));
        if (auto registered = registerParts(*entry, std::move(sidecar), keys, *imageKeys, *shared); !registered)
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
    // The main asset of a .gltf, .glb or .obj file, which loads as a Model and as a Scene.
    static auto isModelFile(const assets::AssetEntry& entry) -> bool
    {
        return entry.key.empty() && assets::importerFor(entry.path) == "model";
    }

    // The scene of the model file 'id' (see serialize::modelScene), with the local IDs of its entities from the
    // file's sidecar: new entities get new IDs, recorded there. A model that fails to load (logged by loadModel)
    // gives the engine cube's scene.
    auto loadModelScene(AssetId id, const reflect::TypeRegistry& types) -> AssetRef<serialize::Scene>
    {
        const auto model = loadModel(id); // Adds the model's parts to the database, which moves its entries
        if (model.state() == AssetState::Failed)
        {
            const auto cube = id != assets::engine::cube ? loadScene(assets::engine::cube, types) : AssetRef<serialize::Scene>{};
            const auto& slot = m_scenes.slot(id);
            slot->state = AssetState::Failed;
            slot->message = model.slot()->message;
            slot->value = cube.shared();
            return AssetRef<serialize::Scene>{ id, slot };
        }

        const auto entry = *m_database.find(id);
        auto [sidecar, keepReason] = readModelSidecar(entry);
        const auto known = sidecar.subAssets.size();
        auto scene = serialize::modelScene(*model, id.value, types, [&sidecar](const std::string& key) { return sidecar.subAssetId(key).value; }, sidecar.settings.strictNames);
        if (!scene)
        {
            const auto& slot = m_scenes.slot(id);
            slot->state = AssetState::Failed;
            slot->message = std::format("'{}': {}", entry.path, serialize::describe(scene.error()));
            return AssetRef<serialize::Scene>{ id, slot };
        }
        saveModelSidecar(entry, sidecar, sidecar.subAssets.size() - known, keepReason);
        return m_scenes.fill(id, std::make_shared<serialize::Scene>(std::move(*scene)));
    }

    // A material file: the template's parameters with the file's values. A bad file, an unknown template or a
    // value the template's parameter cannot take fails the whole material (the checker fallback).
    auto loadMaterialFile(AssetId id, const assets::AssetEntry& entry) -> AssetRef<assets::Material>
    {
        auto text = readFile(m_database.absolutePath(entry));
        if (!text)
            return failMaterial(id, std::format("cannot read '{}'", entry.path));
        auto file = serialize::parseMaterial(*text);
        if (!file)
            return failMaterial(id, std::format("'{}' {}", entry.path, serialize::describe(file.error())));

        // Only one template exists until custom shaders: the engine content's PBR template, built in code
        if (AssetId{ file->templateId } != assets::engine::pbrTemplate)
            return failMaterial(id, std::format("'{}': {} is not a material template", entry.path, AssetId{ file->templateId }));
        auto material = assets::Material::create(*m_device, m_defaults.pbrTemplate, m_framesInFlight);
        if (!material)
            return failMaterial(id, assets::describe(material.error()));

        for (const auto& value : file->parameters.items)
        {
            if (auto set = setParameter(**material, value); !set)
                return failMaterial(id, std::format("'{}' line {}: {}", entry.path, value.line, set.error()));
        }
        return m_materials.fill(id, std::move(*material));
    }

    // Sets the parameter 'value.key' from a material file's value, read as the kind of the parameter's default.
    auto setParameter(assets::Material& material, const reflect::Node& value) -> std::expected<void, std::string>
    {
        const auto* parameter = material.materialTemplate()->parameter(value.key);
        if (!parameter)
            return std::unexpected{ std::format("the template has no parameter '{}'", value.key) };

        auto converted = std::visit(
            [&](const auto& defaultValue) -> std::expected<assets::MaterialValue, std::string> {
                using T = std::decay_t<decltype(defaultValue)>;
                if constexpr (std::same_as<T, std::shared_ptr<assets::Texture>>)
                {
                    auto textureId = reflect::fromNode(reflect::FieldKind::AssetRef, value);
                    if (!textureId)
                        return std::unexpected{ std::format("'{}': {}", value.key, reflect::describe(textureId.error())) };
                    return loadTexture(AssetId{ std::get<std::uint64_t>(*textureId) }).shared(); // The checker if it failed
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
        if (!converted)
            return std::unexpected{ converted.error() };
        if (auto set = material.setParameter(value.key, std::move(*converted)); !set)
            return std::unexpected{ assets::describe(set.error()) };
        return {};
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

    // Every missing material shares one fallback, like the other types share their engine content.
    auto failMaterial(AssetId id, const std::string& message) -> AssetRef<assets::Material>
    {
        if (!m_fallbackMaterial)
        {
            if (auto material = m_defaults.createPbrMaterial(*m_device, m_framesInFlight))
            {
                m_fallbackMaterial = std::move(*material);
                if (const auto checker = loadTexture(assets::engine::checker).shared())
                    (void)m_fallbackMaterial->setParameter(assets::pbr::albedoMap, checker);
            }
        }
        return fail(m_materials, id, "material", message, m_fallbackMaterial);
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
    // says why it must stay as it is.
    auto saveModelSidecar(const assets::AssetEntry& entry, const assets::Sidecar& sidecar, std::size_t added, const std::string& keepReason) const -> void
    {
        if (added == 0)
            return;
        const auto path = assets::sidecarPath(m_database.absolutePath(entry));
        if (!keepReason.empty())
            log::warn("'{}' lacks {} ID(s) and is not rewritten ({}); they are new every run", path.generic_string(), added, keepReason);
        else if (std::ofstream out{ path, std::ios::binary })
            out << assets::writeSidecar(sidecar);
        else
            log::warn("Cannot write the sidecar '{}'; the parts and entities of this model get new IDs next run", path.generic_string());
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

        for (std::size_t i = 0; i < model.textures.size(); ++i)
            m_textures.fill(textureIds[i], model.textures[i].texture);
        for (std::size_t i = 0; i < model.materials.size(); ++i)
            materialRefs[model.materials[i].material.get()] = m_materials.fill(materialIds[i], model.materials[i].material);

        for (std::size_t i = 0; i < model.meshes.size(); ++i)
        {
            for (std::size_t j = 0; j < model.meshes[i].primitives.size(); ++j)
            {
                auto& primitive = model.meshes[i].primitives[j];
                primitive.mesh = m_meshes.fill(meshIds[i][j], primitive.mesh.shared());

                // Primitives without a material share the load's one default material (see Loader).
                auto [it, inserted] = materialRefs.try_emplace(primitive.material.get());
                if (inserted)
                    it->second = m_materials.fill(defaultMaterialId, primitive.material.shared());
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

    Cache<assets::Texture> m_textures;
    Cache<assets::StaticMesh> m_meshes;
    Cache<assets::Material> m_materials;
    Cache<assets::Environment> m_environments;
    Cache<assets::Model> m_models;
    Cache<serialize::Scene> m_scenes;
};

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
    m_defaults{ &m_impl->defaults() },
    m_database{ &m_impl->database() },
    m_importCache{ &m_impl->importCache() }
{
}

AssetLibrary::AssetLibrary(AssetLibrary&&) noexcept = default;
auto AssetLibrary::operator=(AssetLibrary&&) noexcept -> AssetLibrary& = default;
AssetLibrary::~AssetLibrary() = default;

auto AssetLibrary::create(rhi::Device& device, std::uint32_t framesInFlight, const Sources& sources)
    -> std::expected<AssetLibrary, std::string>
{
    auto database = assets::AssetDatabase::scan({
        .roots = { { paths::projectContent(sources.project) }, { sources.engineContent, true } },
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

    // Entries unused for long, or past the size limit, are deleted once per start.
    assets::ImportCache importCache{ sources.importCache };
    (void)importCache.evict();

    return AssetLibrary{ std::make_unique<Impl>(device, framesInFlight, std::move(*defaults), std::move(*ibl), std::move(*database),
        std::move(importCache)) };
}

auto AssetLibrary::rescan() -> std::expected<void, std::string>
{
    if (auto scanned = m_impl->rescan(); !scanned)
        return std::unexpected{ std::format("Failed to rescan the assets: {}", describe(scanned.error())) };
    return {};
}

auto AssetLibrary::find(std::string_view path, std::string_view key) const -> std::optional<AssetId>
{
    return m_database->find(0, path, key);
}

auto AssetLibrary::addFile(std::string_view path) -> std::expected<AssetId, std::string>
{
    auto& database = m_impl->database();
    if (auto added = database.addFile(0, path); !added)
        return std::unexpected{ assets::describe(added.error()) };
    const auto found = database.find(0, path);
    if (!found)
        return std::unexpected{ std::format("'{}' holds no asset", path) };
    return *found;
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

auto AssetLibrary::instantiateScene(world::World& world, const reflect::TypeRegistry& types, AssetId scene)
    -> std::expected<void, std::string>
{
    auto read = m_impl->readScene(scene, types);
    if (!read)
        return std::unexpected{ std::move(read.error()) };
    const auto resolve = [this, &types](const reflect::TypeInfo& type, void* component) { m_impl->resolve(type, component, types); };
    if (auto created = serialize::instantiate(world.registry(), types, *read, resolve); !created)
        return std::unexpected{ std::format("Cannot instantiate scene {}: {}", scene, serialize::describe(created.error())) };
    return {};
}

void AssetLibrary::resolve(const reflect::TypeInfo& type, void* component, const reflect::TypeRegistry& types)
{
    m_impl->resolve(type, component, types);
}

auto AssetLibrary::instantiateScene(world::World& world, const reflect::TypeRegistry& types, AssetId scene, world::Entity parent)
    -> std::expected<world::Entity, std::string>
{
    const auto loaded = m_impl->loadScene(scene, types);
    if (!loaded)
        return std::unexpected{ std::format("Cannot instantiate scene {}: {}", scene, loaded.slot()->message) };
    const auto resolve = [this, &types](const reflect::TypeInfo& type, void* component) { m_impl->resolve(type, component, types); };
    auto holder = serialize::instantiate(world.registry(), types, loaded, parent, resolve);
    if (!holder)
        return std::unexpected{ std::format("Cannot instantiate scene {}: {}", scene, serialize::describe(holder.error())) };
    if (const auto* entry = m_database->find(scene))
        world.registry().get<Name>(*holder).name = std::filesystem::path{ entry->path }.stem().string();
    return *holder;
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
