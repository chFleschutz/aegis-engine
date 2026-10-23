module;

#include <cassert>

// Only the ECS headers: entt.hpp pulls in entt::meta, which crashes clang -O3 against libstdc++
#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>
#include <entt/entity/view.hpp>

module aegis.render_bridge;
import std;
import aegis.assets;
import aegis.core;
import aegis.math;
import aegis.render_graph;
import aegis.rhi;
import aegis.world;
import aegis.world.registry;
import aegis.world.render_components;

namespace aegis
{
namespace
{
auto descriptorOf(const std::shared_ptr<assets::Texture>& texture) -> std::optional<rhi::TextureDescriptor>
{
    if (!texture)
        return std::nullopt;
    return texture->descriptor();
}
}

// Instance bridge ------------------------------------------------------------------------------------

InstanceBridge::InstanceBridge(render_graph::InstanceStore& store) :
    m_store{ store }
{
}

InstanceBridge::~InstanceBridge()
{
    disconnect();
}

auto InstanceBridge::connect(world::Registry& registry) -> void
{
    disconnect();
    m_store.clear();
    m_syncedVersion.reset();
    m_registry = &registry;

    for (auto&& [entity, renderer] : registry.view<MeshRenderer>().each())
        registry.getOrAdd<RenderInstance>(world::Entity{ entity }) = {};

    registry.onConstruct<MeshRenderer>().connect<&InstanceBridge::onRendererConstruct>(this);
    registry.onDestroy<MeshRenderer>().connect<&InstanceBridge::onRendererDestroy>(this);
    registry.onDestroy<RenderInstance>().connect<&InstanceBridge::onInstanceDestroy>(this);
    registry.onConstruct<DynamicTag>().connect<&InstanceBridge::onDynamicConstruct>(this);
    registry.onDestroy<DynamicTag>().connect<&InstanceBridge::onDynamicDestroy>(this);
    registry.invalidateStatic();
}

auto InstanceBridge::disconnect() -> void
{
    if (!m_registry)
        return;

    m_registry->onConstruct<MeshRenderer>().disconnect(this);
    m_registry->onDestroy<MeshRenderer>().disconnect(this);
    m_registry->onDestroy<RenderInstance>().disconnect(this);
    m_registry->onConstruct<DynamicTag>().disconnect(this);
    m_registry->onDestroy<DynamicTag>().disconnect(this);

    // The ids name instances of the store, which is emptied with them.
    std::vector<entt::entity> tracked;
    for (auto&& [entity, instance] : m_registry->view<RenderInstance>().each())
        tracked.push_back(entity);
    for (const auto entity : tracked)
        m_registry->remove<RenderInstance>(world::Entity{ entity });
    m_store.clear();
    m_registry = nullptr;
}

auto InstanceBridge::sync() -> void
{
    if (!m_registry)
        return;

    const auto version = m_registry->staticVersion();
    if (m_syncedVersion != version)
    {
        m_syncedVersion = version;
        auto statics = m_registry->view<RenderInstance, MeshRenderer, GlobalTransform>(entt::exclude<DynamicTag>);
        for (auto&& [entity, instance, renderer, transform] : statics.each())
            refresh(entity, instance, renderer, transform, false);
    }
    // DynamicTag is empty, so the view's each() does not yield it.
    auto dynamics = m_registry->view<RenderInstance, MeshRenderer, GlobalTransform, DynamicTag>();
    for (auto&& [entity, instance, renderer, transform] : dynamics.each())
        refresh(entity, instance, renderer, transform, true);
}

auto InstanceBridge::refresh(entt::entity entity, RenderInstance& instance, const MeshRenderer& renderer,
    const GlobalTransform& transform, bool dynamic) -> void
{
    if (!renderer.mesh || !renderer.material)
    {
        if (instance.id)
            m_store.remove(*instance.id);
        instance.id.reset();
        return;
    }

    const auto model = render_graph::detail::toModelRows(transform.matrix());
    const auto& materialTemplate = renderer.material->materialTemplate();
    if (instance.id)
    {
        // Most instances are the same as at the last sync: compare before copying the shared pointers.
        const auto& current = m_store.get(*instance.id);
        if (current.mesh.get() == renderer.mesh.get() && current.material.get() == renderer.material.get() &&
            m_store.batches()[current.drawBatch].materialTemplate == materialTemplate &&
            render_graph::detail::sameModel(current.model, model))
            return;
    }

    render_graph::Instance drawn{
        .model = model,
        .mesh = renderer.mesh.shared(),
        .material = renderer.material.shared(),
    };
    std::expected<void, render_graph::Error> stored;
    if (instance.id)
    {
        stored = m_store.update(*instance.id, std::move(drawn), materialTemplate);
    }
    else if (auto added = m_store.add(std::move(drawn), materialTemplate, dynamic))
    {
        instance.id = *added;
    }
    else
    {
        stored = std::unexpected{ added.error() };
    }

    if (!stored && !instance.warned)
    {
        log::warn("InstanceBridge: {} not drawn as it is: {}", static_cast<std::uint32_t>(entity), describe(stored.error()));
        instance.warned = true;
    }
}

auto InstanceBridge::setDynamic(entt::registry& registry, entt::entity entity, bool dynamic) -> void
{
    if (const auto* instance = registry.try_get<RenderInstance>(entity); instance && instance->id)
        m_store.setDynamic(*instance->id, dynamic);
    m_registry->invalidateStatic();
}

auto InstanceBridge::onRendererConstruct(entt::registry& registry, entt::entity entity) -> void
{
    registry.emplace<RenderInstance>(entity);
    // The next sync() adds it: every frame for a dynamic entity, and once its global transform is computed
    // for a static one.
    if (registry.all_of<DynamicTag>(entity))
        m_registry->invalidateStatic();
    else
        m_registry->markStaticDirty(world::Entity{ entity });
}

auto InstanceBridge::onRendererDestroy(entt::registry& registry, entt::entity entity) -> void
{
    // Already gone if the entity is being destroyed and its RenderInstance went first.
    registry.remove<RenderInstance>(entity);
    m_registry->invalidateStatic();
}

auto InstanceBridge::onInstanceDestroy(entt::registry& registry, entt::entity entity) -> void
{
    if (const auto& id = registry.get<RenderInstance>(entity).id)
        m_store.remove(*id);
}

auto InstanceBridge::onDynamicConstruct(entt::registry& registry, entt::entity entity) -> void
{
    setDynamic(registry, entity, true);
}

auto InstanceBridge::onDynamicDestroy(entt::registry& registry, entt::entity entity) -> void
{
    setDynamic(registry, entity, false);
}

// Frame scene ----------------------------------------------------------------------------------------

auto extractFrameScene(world::World& world, const render_graph::InstanceStore& instances) -> render_graph::FrameScene
{
    using render_graph::FrameScene;
    auto& registry = world.registry();
    FrameScene scene{ .instances = instances };

    if (const auto camera = world.mainCamera(); camera && registry.has<Camera, GlobalTransform>(camera))
    {
        const auto& component = registry.get<Camera>(camera);
        scene.camera = FrameScene::Camera{
            .view = component.viewMatrix,
            .projection = component.projectionMatrix,
            .position = registry.get<GlobalTransform>(camera).location,
        };
    }
    if (const auto ambient = world.ambientLight(); ambient && registry.has<AmbientLight>(ambient))
    {
        const auto& light = registry.get<AmbientLight>(ambient);
        scene.ambientLight = FrameScene::AmbientLight{ .color = light.color, .intensity = light.intensity };
    }
    if (const auto directional = world.directionalLight();
        directional && registry.has<DirectionalLight, GlobalTransform>(directional))
    {
        const auto& light = registry.get<DirectionalLight>(directional);
        scene.directionalLight = FrameScene::DirectionalLight{
            .color = light.color,
            .intensity = light.intensity,
            .direction = glm::normalize(registry.get<GlobalTransform>(directional).forward()),
        };
    }
    for (auto&& [entity, transform, light] : registry.view<GlobalTransform, PointLight>().each())
    {
        scene.pointLights.push_back({
            .position = transform.location,
            .color = light.color,
            .intensity = light.intensity,
            .scale = transform.scale.x,
        });
    }
    if (const auto sky = registry.first<Sky>())
    {
        if (const auto& environment = registry.get<Sky>(sky).environment)
        {
            scene.environment = {
                .irradiance = descriptorOf(environment->irradiance),
                .prefiltered = descriptorOf(environment->prefiltered),
                .skybox = descriptorOf(environment->skybox),
            };
        }
    }
    return scene;
}

// Worlds ---------------------------------------------------------------------------------------------

Worlds::Worlds(Setup setup) :
    m_setup{ std::move(setup) }
{
}

Worlds::~Worlds() = default;

auto Worlds::create() -> world::World&
{
    auto& entry = *m_entries.emplace_back(std::make_unique<Entry>());
    entry.bridge.connect(entry.world.registry());
    m_setup(entry.world);
    return entry.world;
}

auto Worlds::destroy(world::World& world) -> void
{
    const auto found = std::ranges::find(m_entries, &world, [](const auto& entry) { return &entry->world; });
    assert(found != m_entries.end() && "Worlds::destroy: not one of these worlds");
    m_entries.erase(found);
}

auto Worlds::reset(world::World& world) -> void
{
    auto& found = entry(world);
    world.reset();
    m_setup(world);
    found.begun = false;
}

auto Worlds::update(world::World& world, float deltaSeconds, world::UpdateMode mode) -> void
{
    auto& found = entry(world);
    if (!found.begun)
    {
        world.begin(mode);
        found.begun = true;
    }
    world.update(deltaSeconds, mode);
}

auto Worlds::sync(world::World& world) -> void
{
    entry(world).bridge.sync();
}

auto Worlds::contains(const world::World& world) const -> bool
{
    return std::ranges::any_of(m_entries, [&world](const auto& entry) { return &entry->world == &world; });
}

auto Worlds::all() -> std::vector<world::World*>
{
    std::vector<world::World*> worlds;
    worlds.reserve(m_entries.size());
    for (const auto& entry : m_entries)
        worlds.push_back(&entry->world);
    return worlds;
}

auto Worlds::instances(const world::World& world) -> render_graph::InstanceStore&
{
    return entry(world).instances;
}

auto Worlds::entry(const world::World& world) -> Entry&
{
    const auto found = std::ranges::find(m_entries, &world, [](const auto& entry) -> const world::World* { return &entry->world; });
    assert(found != m_entries.end() && "Worlds: not one of these worlds");
    return **found;
}
}
