module aegis.serialize;
import std;
import aegis.assets;
import aegis.reflect;
import aegis.world.registry;
import aegis.world.render_components;

namespace aegis::serialize
{
namespace
{
using namespace detail;

// Builds the entities of one model scene, keyed like sub-assets.
class ModelSceneBuilder
{
public:
    ModelSceneBuilder(const reflect::TypeInfo& transform, const reflect::TypeInfo& meshRenderer, const ModelEntityId& entityId, bool strictNames) :
        m_transform{ transform },
        m_meshRenderer{ meshRenderer },
        m_entityId{ entityId },
        m_keys{ strictNames }
    {
    }

    // The key of the node `position` (its index among the model's nodes) called `name`.
    auto nodeKey(std::string_view name, std::size_t position) -> std::expected<std::string, Error>
    {
        auto key = m_keys.name(assets::SubAssetKeys::entityType, name, position);
        if (!key)
            return makeError(ErrorCode::InvalidData, std::format("node {}: {}", position, key.error().message));
        return std::move(*key);
    }

    // Adds the entity `key` and returns its index in entities().
    auto add(const std::string& key, std::string name, const Transform& transform) -> std::expected<std::size_t, Error>
    {
        SceneEntity entity{ .id = m_entityId(key), .name = std::move(name) };
        auto saved = saveFields(m_transform, &transform);
        if (!saved)
            return std::unexpected{ std::move(saved.error()) };
        entity.components.push_back(std::move(*saved));
        m_entities.push_back(std::move(entity));
        return m_entities.size() - 1;
    }

    auto addChild(std::size_t parent, std::size_t child) -> void { m_entities[parent].children.push_back(m_entities[child].id); }

    // Draws `mesh` on the entity `index` (keyed `key`): one primitive on the entity itself, several on child
    // entities.
    auto attachMesh(std::size_t index, const std::string& key, const assets::ModelGpuMesh& mesh) -> std::expected<void, Error>
    {
        if (mesh.primitives.size() == 1)
            return addRenderer(index, mesh.primitives[0]);
        const std::string name = m_entities[index].name;
        for (std::size_t sub = 0; sub < mesh.primitives.size(); ++sub)
        {
            auto child = add(assets::SubAssetKeys::withIndex(key, sub), std::format("{}_Submesh_{}", name, sub), Transform{});
            if (!child)
                return std::unexpected{ std::move(child.error()) };
            if (auto added = addRenderer(*child, mesh.primitives[sub]); !added)
                return added;
            addChild(index, *child);
        }
        return {};
    }

    // The scene `id` of the entities, sorted by ID.
    auto finish(std::uint64_t id) && -> std::expected<Scene, Error>
    {
        Scene scene{ .id = id, .entities = std::move(m_entities) };
        std::ranges::sort(scene.entities, {}, &SceneEntity::id);
        const auto repeated = std::ranges::adjacent_find(scene.entities, {}, &SceneEntity::id);
        if (repeated != scene.entities.end())
            return makeError(ErrorCode::DuplicateEntity, std::format("the model's entity ID {} is given twice", formatId(repeated->id)));
        return scene;
    }

private:
    auto addRenderer(std::size_t index, const assets::ModelGpuPrimitive& primitive) -> std::expected<void, Error>
    {
        const MeshRenderer renderer{ .mesh = primitive.mesh, .material = primitive.material };
        auto saved = saveFields(m_meshRenderer, &renderer);
        if (!saved)
            return std::unexpected{ std::move(saved.error()) };
        auto& components = m_entities[index].components;
        components.insert(components.begin(), std::move(*saved)); // Sorted by name: MeshRenderer before Transform
        return {};
    }

    const reflect::TypeInfo& m_transform;
    const reflect::TypeInfo& m_meshRenderer;
    const ModelEntityId& m_entityId;
    assets::SubAssetKeys m_keys;
    std::vector<SceneEntity> m_entities;
};
}

auto modelScene(const assets::Model& model, std::uint64_t id, const reflect::TypeRegistry& types, const ModelEntityId& entityId, bool strictNames)
    -> std::expected<Scene, Error>
{
    const reflect::TypeInfo* transform = types.find<Transform>();
    const reflect::TypeInfo* meshRenderer = types.find<MeshRenderer>();
    if (!transform || !meshRenderer)
        return makeError(ErrorCode::UnknownType, "a model's scene needs Transform and MeshRenderer to be registered");

    ModelSceneBuilder builder{ *transform, *meshRenderer, entityId, strictNames };
    const std::string rootKey{ assets::SubAssetKeys::rootEntity };
    auto root = builder.add(rootKey, model.name, Transform{ .rotation = model.rootRotation });
    if (!root)
        return std::unexpected{ std::move(root.error()) };
    if (model.rootMesh)
    {
        if (auto attached = builder.attachMesh(*root, rootKey, model.meshes.at(*model.rootMesh)); !attached)
            return std::unexpected{ std::move(attached.error()) };
    }

    std::vector<std::size_t> entities; // Of each node
    entities.reserve(model.nodes.size());
    for (std::size_t i = 0; i < model.nodes.size(); ++i)
    {
        const auto& node = model.nodes[i];
        auto key = builder.nodeKey(node.name, i);
        if (!key)
            return std::unexpected{ std::move(key.error()) };
        auto entity = builder.add(*key, node.name, Transform{ .location = node.translation, .rotation = node.rotation, .scale = node.scale });
        if (!entity)
            return std::unexpected{ std::move(entity.error()) };
        entities.push_back(*entity);
        builder.addChild(node.parent ? entities.at(*node.parent) : *root, *entity);

        if (node.mesh)
        {
            if (auto attached = builder.attachMesh(*entity, *key, model.meshes.at(*node.mesh)); !attached)
                return std::unexpected{ std::move(attached.error()) };
        }
    }
    return std::move(builder).finish(id);
}
}
