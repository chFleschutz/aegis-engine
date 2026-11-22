module aegis.scene_description;

import std;

namespace aegis
{
auto instantiate(SceneContext& context, assets::AssetId scene, world::Entity parent) -> std::expected<world::Entity, AssetLibraryError>
{
    return context.assets.instantiateScene(context.scene, context.types, scene, parent);
}
}
