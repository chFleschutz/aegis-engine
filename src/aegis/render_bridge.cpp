module;

// Only the ECS headers: entt.hpp pulls in entt::meta, which crashes clang -O3 against libstdc++
#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>
#include <entt/entity/view.hpp>

module aegis.render_bridge;
import std;
import aegis.assets;
import aegis.base;
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
    m_tracked.clear();
    m_syncedVersion.reset();
    m_registry = &registry;

    for (auto&& [entity, renderer] : registry.view<MeshRenderer>().each())
    {
        m_tracked.emplace(entity, Tracked{ .dynamic = registry.has<DynamicTag>(world::Entity{ entity }), .warned = false });
    }

    registry.onConstruct<MeshRenderer>().connect<&InstanceBridge::onRendererConstruct>(this);
    registry.onDestroy<MeshRenderer>().connect<&InstanceBridge::onRendererDestroy>(this);
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
    m_registry->onConstruct<DynamicTag>().disconnect(this);
    m_registry->onDestroy<DynamicTag>().disconnect(this);
    m_registry = nullptr;
}

auto InstanceBridge::sync() -> void
{
    if (!m_registry)
        return;

    const auto version = m_registry->staticVersion();
    const bool staticChanged = m_syncedVersion != version;
    m_syncedVersion = version;
    // In the registry's order, so instances are added in the order the world holds them.
    const auto refreshAll = [this](auto view) {
        for (const auto entity : view)
        {
            if (const auto found = m_tracked.find(entity); found != m_tracked.end())
                refresh(entity, found->second);
        }
    };
    if (staticChanged)
        refreshAll(m_registry->view<MeshRenderer>());
    else
        refreshAll(m_registry->view<MeshRenderer, DynamicTag>());
}

auto InstanceBridge::refresh(entt::entity entity, Tracked& tracked) -> void
{
    const world::Entity handle{ entity };
    const auto& renderer = m_registry->get<MeshRenderer>(handle);
    if (!renderer.mesh || !renderer.material || !m_registry->has<GlobalTransform>(handle))
    {
        if (tracked.instance)
            m_store.remove(*tracked.instance);
        tracked.instance.reset();
        return;
    }

    render_graph::Instance instance{
        .transform = m_registry->get<GlobalTransform>(handle).matrix(),
        .mesh = renderer.mesh.shared(),
        .material = renderer.material.shared(),
        .materialTemplate = renderer.material->materialTemplate(),
    };
    std::expected<void, render_graph::Error> stored;
    if (tracked.instance)
    {
        stored = m_store.update(*tracked.instance, std::move(instance));
    }
    else if (auto added = m_store.add(std::move(instance), tracked.dynamic))
    {
        tracked.instance = *added;
    }
    else
    {
        stored = std::unexpected{ added.error() };
    }

    if (!stored && !tracked.warned)
    {
        log::warn("InstanceBridge: {} not drawn as it is: {}", static_cast<std::uint32_t>(entity), describe(stored.error()));
        tracked.warned = true;
    }
}

auto InstanceBridge::setDynamic(entt::entity entity, bool dynamic) -> void
{
    const auto found = m_tracked.find(entity);
    if (found == m_tracked.end() || found->second.dynamic == dynamic)
        return;

    found->second.dynamic = dynamic;
    if (found->second.instance)
        m_store.setDynamic(*found->second.instance, dynamic);
}

auto InstanceBridge::onRendererConstruct(entt::registry& registry, entt::entity entity) -> void
{
    const bool dynamic = registry.all_of<DynamicTag>(entity);
    m_tracked.insert_or_assign(entity, Tracked{ .dynamic = dynamic, .warned = false });
    // The next sync() adds it: every frame for a dynamic entity, and once its global transform is computed
    // for a static one.
    if (dynamic)
        m_registry->invalidateStatic();
    else
        m_registry->markStaticDirty(world::Entity{ entity });
}

auto InstanceBridge::onRendererDestroy(entt::registry&, entt::entity entity) -> void
{
    if (const auto found = m_tracked.find(entity); found != m_tracked.end())
    {
        if (found->second.instance)
            m_store.remove(*found->second.instance);
        m_tracked.erase(found);
    }
    m_registry->invalidateStatic();
}

auto InstanceBridge::onDynamicConstruct(entt::registry&, entt::entity entity) -> void
{
    setDynamic(entity, true);
    m_registry->invalidateStatic();
}

auto InstanceBridge::onDynamicDestroy(entt::registry&, entt::entity entity) -> void
{
    setDynamic(entity, false);
    m_registry->invalidateStatic();
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
}
