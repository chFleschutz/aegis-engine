module;

#include <cstdlib>

module aegis.scene_description;

import std;
import aegis.core;

namespace aegis
{
auto instantiate(SceneContext& context, assets::AssetId scene, world::Entity parent) -> world::Entity
{
    auto holder = context.assets.instantiateScene(context.scene, context.types, scene, parent);
    if (!holder)
    {
        log::fatal("{}", holder.error());
        std::exit(EXIT_FAILURE);
    }
    return *holder;
}
}
