module aegis.test;

import std;
import aegis.asset_library;
import aegis.assets;
import aegis.paths;
import aegis.reflect;
import aegis.renderer;
import aegis.rhi;
import aegis.math;
import aegis.world;
import aegis.world.registry;
import aegis.world.render_components;
import aegis.world.types;

namespace aegis::test
{
// Renderer textures must reach shaders through bindless, cover all their mips, and keep their
// bindless slot across a resize. The name is a temporary on purpose: the renderer must copy it.
// aegis.assets against a real device: default assets, a texture read back after its upload left
// it in ShaderRead, per-frame material blocks, and glTF + OBJ files instantiated as scenes.
auto Harness::verifyAssets() -> std::expected<void, std::string>
{
    constexpr auto framesInFlight = renderer::Renderer::maxFramesInFlight;
    auto defaults = assets::DefaultAssets::create(*m_device, framesInFlight);
    if (!defaults)
        return std::unexpected{ std::format("Default assets: {}", assets::describe(defaults.error())) };
    if (auto result = flushAndWait(); !result)
        return result;

    // The white texture: 1x1 RGBA32_SFLOAT, so 16 bytes of 1.0f.
    auto readback = m_device->createBuffer({
        .name = "AssetTestReadback",
        .size = 4 * sizeof(float),
        .usage = rhi::BufferUsage::TransferDst,
        .memory = rhi::MemoryUsage::CpuRead,
    });
    if (!readback)
        return std::unexpected{ "Failed to create asset readback buffer" };
    const std::array regions{ rhi::BufferImageCopy{ .extent = { 1, 1, 1 } } };
    auto copied = submitAndWait("AssetTestReadback", [&](const auto& cmd) {
        cmd.transitionImageLayout(defaults->white->image(), rhi::ResourceState::ShaderRead,
            rhi::ResourceState::CopySrc);
        cmd.copyImageToBuffer(defaults->white->image(), m_device->get(*readback), regions);
        cmd.transitionImageLayout(defaults->white->image(), rhi::ResourceState::CopySrc,
            rhi::ResourceState::ShaderRead);
    });
    if (!copied)
        return copied;
    std::array<float, 4> white{};
    m_device->get(*readback).read(reinterpret_cast<std::byte*>(white.data()), sizeof(white));
    m_device->free(*readback);
    if (white != std::array{ 1.0f, 1.0f, 1.0f, 1.0f })
        return std::unexpected{ std::format("Default white texture reads back as {}", white) };

    auto& material = *defaults->pbrMaterial;
    for (std::uint32_t frame = 0; frame < framesInFlight; ++frame)
        material.update(frame);
    if (material.parameterAddress(0) == 0 || material.parameterAddress(1) == material.parameterAddress(0))
        return std::unexpected{ "Material parameter blocks are null or shared between frames" };

    // Caller bugs come back as errors instead of corrupting a parameter block or over-reading.
    const auto rejected = [](const auto& result) {
        return !result && result.error().code == assets::ErrorCode::InvalidArgument;
    };
    if (!rejected(material.setParameter("roughness", 1)) ||
        !rejected(material.setParameter("albedoMap", std::shared_ptr<assets::Texture>{})))
        return std::unexpected{ "A material accepted a value of the wrong type or a null texture" };
    const std::array<float, 4> oneTexel{};
    const auto wrongSize = assets::Texture::create(*m_device,
        { .name = "WrongSize", .extent = { 2, 2, 1 }, .format = rhi::Format::RGBA32_SFLOAT, .mipLevels = 1 },
        std::as_bytes(std::span{ oneTexel }));
    if (!rejected(wrongSize))
        return std::unexpected{ "Texture::create accepted 16 bytes as mip 0 of a 2x2 RGBA32_SFLOAT texture" };

    // The models as scene instances, loaded by an asset library (with its own default assets).
    auto library = AssetLibrary::create(*m_device, framesInFlight);
    if (!library)
        return std::unexpected{ std::format("Asset library: {}", library.error()) };
    reflect::TypeRegistry types;
    if (auto registered = world::registerComponents(types); !registered)
        return std::unexpected{ std::format("Component types: {}", reflect::describe(registered.error())) };
    aegis::world::World models;
    for (const auto* model : { "models/damaged_helmet/DamagedHelmet.gltf", "models/basic/cube.obj" })
    {
        const auto id = library->find(model);
        if (!id)
            return std::unexpected{ std::format("'{}' is not in the sample project", model) };
        if (auto holder = library->instantiateScene(models, types, *id, {}); !holder)
            return std::unexpected{ std::format("Instantiating {}: {}", model, holder.error()) };
    }
    const assets::Loader::Desc loaderDesc{
        .device = *m_device,
        .defaults = *defaults,
        .framesInFlight = framesInFlight,
    };
    // Each load gets its own default material: editing one model's must not change another's (E-CONTENT-04).
    {
        std::array<std::shared_ptr<assets::Material>, 2> defaultMaterials;
        for (auto& slot : defaultMaterials)
        {
            auto cube = assets::Loader::load(loaderDesc, paths::projectContent(paths::project()) / "models/basic/cube.obj");
            if (!cube)
                return std::unexpected{ std::format("Loading cube.obj: {}", assets::describe(cube.error())) };
            slot = cube->meshes.at(0).primitives.at(0).material.shared();
        }
        if (!defaultMaterials[0] || !defaultMaterials[1] || defaultMaterials[0] == defaultMaterials[1] ||
            defaultMaterials[0] == defaults->pbrMaterial)
            return std::unexpected{ "Two loads share a default material" };
    }
    if (auto result = flushAndWait(); !result)
        return result;

    std::size_t meshes{ 0 };
    std::size_t texturedMaterials{ 0 };
    for (auto&& [entity, renderer] : models.registry().view<aegis::MeshRenderer>().each())
    {
        if (!renderer.mesh || renderer.mesh->indexCount() == 0 || renderer.mesh->meshDataAddress() == 0)
            return std::unexpected{ "A loaded mesh is empty or has no device address" };
        if (!renderer.material)
            return std::unexpected{ "A loaded mesh has no material" };
        const auto* albedoMap = renderer.material->parameter<std::shared_ptr<assets::Texture>>("albedoMap");
        if (!albedoMap || !*albedoMap)
            return std::unexpected{ "A loaded material has no albedo map" };
        const auto& albedo = *albedoMap;
        if (albedo != library->defaults().white)
        {
            // The helmet's albedo: decoded at full size, with GPU-generated mips.
            const auto& image = m_device->get(albedo->image());
            if (image.extent().x != 2048 || image.extent().y != 2048 || image.mipLevels() != 12)
            {
                return std::unexpected{ std::format("Loaded albedo map is {}x{} with {} mips, want 2048x2048 with 12",
                    image.extent().x, image.extent().y, image.mipLevels()) };
            }
            ++texturedMaterials;
        }
        ++meshes;
    }
    if (meshes != 2 || texturedMaterials != 1)
    {
        return std::unexpected{
            std::format("Expected 2 meshes, 1 with its own albedo map; got {} and {}", meshes, texturedMaterials)
        };
    }

    std::println("Assets verified: defaults (white texture read back), material blocks, invalid parameters "
                 "and mip 0 sizes rejected, glTF + OBJ instantiated as scenes ({} meshes)",
        meshes);
    return {};
}

// AssetLibrary against a real device: an ID loads once, the parts of a model are the assets their own IDs
// load, and a missing ID fails loudly into engine content instead of leaving a hole.
auto Harness::verifyAssetLibrary() -> std::expected<void, std::string>
{
    constexpr auto framesInFlight = renderer::Renderer::maxFramesInFlight;
    auto library = AssetLibrary::create(*m_device, framesInFlight);
    if (!library)
        return std::unexpected{ std::format("Asset library: {}", library.error()) };
    const auto fail = [](std::string message) -> std::expected<void, std::string> { return std::unexpected{ std::move(message) }; };

    const auto helmetFile = library->find("models/damaged_helmet/DamagedHelmet.gltf");
    if (!helmetFile)
        return fail("The helmet has no ID in the database");
    const auto first = library->loadModel(*helmetFile);
    const auto second = library->loadModel(*helmetFile);
    if (first.state() != assets::AssetState::Ready || !first || first.get() != second.get())
        return fail("Loading one model ID twice must return the same ready model");

    const auto& model = *first;
    if (model.textures.empty() || model.materials.empty() || model.meshes.empty())
        return fail("The helmet model has no textures, materials or meshes");
    std::size_t parts{ 0 };
    for (const auto& primitive : model.meshes.front().primitives)
    {
        const auto* entry = library->database().find(primitive.mesh.id());
        if (!entry || entry->key.empty() || entry->path != "models/damaged_helmet/DamagedHelmet.gltf")
            return fail("A primitive's mesh has no sub-asset ID in the database");
        if (library->loadMesh(primitive.mesh.id()).get() != primitive.mesh.get())
            return fail("Loading a mesh by its own ID must return the model's mesh");
        if (library->loadMaterial(primitive.material.id()).get() != primitive.material.get())
            return fail("Loading a material by its own ID must return the model's material");
        ++parts;
    }
    for (const auto& texture : model.textures)
    {
        const auto id = library->database().find(0, "models/damaged_helmet/DamagedHelmet.gltf", std::format("Texture/{}", std::filesystem::path{ texture.uri }.filename().generic_string()));
        if (!id || library->loadTexture(*id).get() != texture.texture.get())
            return fail("Loading a glTF texture by its own ID must return the model's texture");
    }

    const auto paintingId = library->id("materials/textures/painting.png");
    if (library->loadTexture(paintingId).get() != library->loadTexture(paintingId).get() || !library->loadTexture(paintingId))
        return fail("Loading one texture ID twice must return the same texture");

    // Engine content is found like any other asset, in the read-only root.
    for (const auto id : { assets::engine::white, assets::engine::black, assets::engine::normal, assets::engine::checker })
    {
        const auto texture = library->loadTexture(id);
        const auto* entry = library->database().find(id);
        if (texture.state() != assets::AssetState::Ready || !entry || entry->root != 1)
            return fail("An engine content texture is missing or not in the engine content root");
    }

    // A missing ID is Failed with a fallback of its type.
    const assets::AssetId missing{ 0x0123456789abcdefULL };
    const auto texture = library->loadTexture(missing);
    const auto mesh = library->loadMesh(missing);
    const auto material = library->loadMaterial(missing);
    const auto environment = library->loadEnvironment(missing);
    const auto missingModel = library->loadModel(missing);
    if (texture.state() != assets::AssetState::Failed || !texture || texture.get() != library->loadTexture(assets::engine::checker).get())
        return fail("A missing texture must fall back to the checker");
    if (mesh.state() != assets::AssetState::Failed || !mesh)
        return fail("A missing mesh must fall back to the cube");
    if (material.state() != assets::AssetState::Failed || !material)
        return fail("A missing material must fall back to a default material");
    if (environment.state() != assets::AssetState::Failed || !environment)
        return fail("A missing environment must fall back to a black sky");
    if (missingModel.state() != assets::AssetState::Failed || !missingModel)
        return fail("A missing model must fall back to the cube");

    // A run-time asset gets its own ID and loads back as itself.
    const auto added = library->add(library->createMaterial().shared());
    if (library->loadMaterial(added.id()).get() != added.get())
        return fail("A run-time asset must load back by its ID");

    // A copy of a shared material is its own: changing it leaves the original as it was.
    const auto& shared = model.meshes.front().primitives.front().material;
    const auto copy = library->copyMaterial(shared);
    if (!copy || copy.get() == shared.get() || copy.id() == shared.id() ||
        copy->materialTemplate() != shared->materialTemplate())
        return fail("copyMaterial must give a new material of the same template under a new ID");
    const auto albedoOf = [](const assets::Material& material) -> std::optional<glm::vec3> {
        const auto* value = material.parameter<glm::vec3>(assets::pbr::albedo);
        return value ? std::optional{ *value } : std::nullopt;
    };
    const auto same = [](const std::optional<glm::vec3>& a, const std::optional<glm::vec3>& b) {
        return a && b && a->x == b->x && a->y == b->y && a->z == b->z;
    };
    const auto original = albedoOf(*shared);
    if (!same(albedoOf(*copy), original))
        return fail("A material copy must start with the original's parameters");
    if (auto set = copy->setParameter(assets::pbr::albedo, glm::vec3{ 0.25f, 0.5f, 0.75f }); !set)
        return fail(std::format("Recoloring a material copy: {}", assets::describe(set.error())));
    if (!same(albedoOf(*shared), original) || same(albedoOf(*copy), original))
        return fail("Changing a material copy must change it and not the shared material");

    // The engine cube's parts have the IDs its read-only sidecar records (a scan of the folder alone),
    // including the default material, so references to them survive a restart.
    const auto content = assets::AssetDatabase::scan({ .roots = { { paths::content(), true } } });
    if (!content)
        return fail(std::format("Scanning the engine content: {}", assets::describe(content.error())));
    const auto cube = library->loadModel(assets::engine::cube);
    const auto& cubePrimitive = cube->meshes.at(0).primitives.at(0);
    if (cubePrimitive.mesh.id() != content->find(0, "cube.obj", "StaticMesh/cube/0") ||
        cubePrimitive.material.id() != content->find(0, "cube.obj", assets::SubAssetKeys::defaultMaterial))
        return fail("The engine cube's mesh and default material must have the IDs of its sidecar");

    // A model file's main ID is its scene. As a world's root scene its entities keep their local IDs: those its
    // sidecar records, so they survive a restart. The helmet is loaded already; the SciFiHelmet loads with its
    // scene, which adds its parts to the database first.
    reflect::TypeRegistry types;
    if (auto registered = world::registerComponents(types); !registered)
        return fail(std::format("Component types: {}", reflect::describe(registered.error())));
    for (const std::string_view path : { "models/damaged_helmet/DamagedHelmet.gltf", "models/sci_fi_helmet/SciFiHelmet.gltf" })
    {
        const auto id = library->find(path);
        if (!id)
            return fail(std::format("'{}' has no ID in the database", path));
        aegis::world::World modelWorld;
        if (auto created = library->instantiateScene(modelWorld, types, *id); !created)
            return fail(std::format("Instantiating the scene of '{}': {}", path, created.error()));
        std::set<std::uint64_t> entityIds;
        for (const auto handle : modelWorld.registry().view<aegis::StableId>())
            entityIds.insert(modelWorld.registry().get<aegis::StableId>(aegis::world::Entity{ handle }).value);

        std::ifstream stream{ assets::sidecarPath(paths::projectContent(paths::project()) / path), std::ios::binary };
        const std::string text{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
        const auto sidecar = assets::parseSidecar(text);
        if (!sidecar)
            return fail(std::format("The sidecar of '{}': {}", path, assets::describe(sidecar.error())));
        std::set<std::uint64_t> recorded;
        for (const auto& [key, entityId] : sidecar->subAssets)
        {
            if (assets::SubAssetKeys::isEntityKey(key))
                recorded.insert(entityId.value);
        }
        if (entityIds.size() < 2 || entityIds != recorded)
            return fail(std::format("The scene of '{}' has {} entities; its sidecar records {} entity IDs, which must be theirs", path,
                entityIds.size(), recorded.size()));
    }

    if (auto result = flushAndWait(); !result)
        return result;
    std::println("Asset library verified: one load per ID, {} mesh parts and the textures shared by ID, engine content, fallbacks, "
                 "model entity IDs in the sidecar",
        parts);
    return {};
}
}
