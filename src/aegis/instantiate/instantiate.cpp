module aegis.instantiate;

import std;
import aegis.math;

namespace aegis
{
namespace
{
/// @brief One primitive goes on the entity itself; several become child entities.
auto attachMesh(world::Registry& registry, world::Entity entity, std::string_view name, const assets::ModelGpuMesh& mesh) -> void
{
    if (mesh.primitives.size() == 1)
    {
        registry.add<Mesh>(entity, mesh.primitives[0].mesh);
        registry.add<Material>(entity, mesh.primitives[0].material);
        return;
    }
    for (std::size_t sub = 0; sub < mesh.primitives.size(); ++sub)
    {
        const auto child = registry.create(std::format("{}_Submesh_{}", name, sub));
        registry.add<Mesh>(child, mesh.primitives[sub].mesh);
        registry.add<Material>(child, mesh.primitives[sub].material);
        registry.addChild(entity, child);
    }
}
}

auto instantiate(world::Registry& registry, const assets::Model& model) -> world::Entity
{
    const auto root = registry.create(model.name);
    registry.get<Transform>(root).rotation = model.rootRotation;
    if (model.rootMesh)
        attachMesh(registry, root, model.name, model.meshes[*model.rootMesh]);

    std::vector<world::Entity> entities(model.nodes.size());
    for (std::size_t i = 0; i < model.nodes.size(); ++i)
    {
        const auto& node = model.nodes[i];
        const auto entity = registry.create(node.name, node.translation, node.rotation, node.scale);
        entities[i] = entity;
        if (node.parent)
            registry.addChild(entities[*node.parent], entity);
        else
            registry.setParent(entity, root);

        if (node.mesh)
            attachMesh(registry, entity, node.name, model.meshes[*node.mesh]);
    }
    return root;
}
}
