module aegis.render_graph;
import std;
import aegis.assets;
import aegis.math;
import aegis.rhi;
import aegis.world;
import aegis.world.render_components;

namespace aegis::render_graph
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

auto extract(aegis::world::World& world) -> FrameScene
{
    auto& registry = world.registry();
    FrameScene scene;

    if (const auto camera = world.mainCamera(); camera && registry.has<aegis::Camera, aegis::GlobalTransform>(camera))
    {
        const auto& component = registry.get<aegis::Camera>(camera);
        scene.camera = FrameScene::Camera{
            .view = component.viewMatrix,
            .projection = component.projectionMatrix,
            .position = registry.get<aegis::GlobalTransform>(camera).location,
        };
    }
    if (const auto ambient = world.ambientLight(); ambient && registry.has<aegis::AmbientLight>(ambient))
    {
        const auto& light = registry.get<aegis::AmbientLight>(ambient);
        scene.ambientLight = FrameScene::AmbientLight{ .color = light.color, .intensity = light.intensity };
    }
    if (const auto directional = world.directionalLight();
        directional && registry.has<aegis::DirectionalLight, aegis::GlobalTransform>(directional))
    {
        const auto& light = registry.get<aegis::DirectionalLight>(directional);
        scene.directionalLight = FrameScene::DirectionalLight{
            .color = light.color,
            .intensity = light.intensity,
            .direction = glm::normalize(registry.get<aegis::GlobalTransform>(directional).forward()),
        };
    }
    for (auto&& [entity, transform, light] : registry.view<aegis::GlobalTransform, aegis::PointLight>().each())
    {
        scene.pointLights.push_back({
            .position = transform.location,
            .color = light.color,
            .intensity = light.intensity,
            .scale = transform.scale.x,
        });
    }
    if (const auto sky = registry.first<aegis::Sky>())
    {
        if (const auto& environment = registry.get<aegis::Sky>(sky).environment)
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
