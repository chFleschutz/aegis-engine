export module aegis.world.types;

import std;
import aegis.assets;
import aegis.reflect;
import aegis.world.registry;
import aegis.world.render_components;

export namespace aegis::world
{
/// @brief Reaches a registered component on an entity without naming its type, for code that works only
///        through the type registry (the scene serializer, the editor). registerComponent attaches it to the type.
/// @note A tag (an empty type) has no storage: find's pointer is to a shared dummy, and it has no fields.
struct ComponentStorage
{
    /// @brief Acts on the entity's component of the type (see changed)
    using Hook = std::function<void(Registry&, Entity)>;

    /// @brief The entity's component, or null if it has none
    std::function<void*(Registry&, Entity)> find;
    /// @brief Gives the entity 'value' (an instance of the type, moved from), replacing the component it has.
    ///        Build the value first: observers of the component's construction (the draw batches) see it as it
    ///        is added.
    std::function<void(Registry&, Entity, void* value)> add;
    /// @brief Removes the entity's component. False (and nothing removed) if it has none or the type is required
    ///        (ComponentTraits, reflect::TypeMeta::required).
    std::function<bool(Registry&, Entity)> remove;
    /// @brief Call it after writing the entity's component through find's pointer, which entt cannot observe. It
    ///        keeps the type's rule for such writes in one place (D2): a static entity's Transform marks it
    ///        static-dirty, a MeshRenderer invalidates the static set. Does nothing for the other types.
    Hook changed;
    /// @brief The first entity that has one (a null Entity if none does)
    std::function<Entity(Registry&)> first;
    /// @brief A world holds one at most (the sky, the main camera): instantiating a scene into a world that
    ///        already has one drops the scene's with a warning, so the world's first wins
    bool singleton{ false };
};

/// @brief What ComponentStorage::changed does for T
template<typename T>
auto changedHook() -> ComponentStorage::Hook
{
    if constexpr (std::same_as<T, Transform>)
    {
        // A dynamic entity's global transform is recomputed every frame anyway
        return [](Registry& registry, Entity entity) {
            if (!registry.has<DynamicTag>(entity))
                registry.markStaticDirty(entity);
        };
    }
    else if constexpr (std::same_as<T, MeshRenderer>)
    {
        // The instance bridge re-reads the static entities' mesh and material
        return [](Registry& registry, Entity) { registry.invalidateStatic(); };
    }
    else
    {
        return [](Registry&, Entity) {};
    }
}

/// @brief The ComponentStorage of T
template<typename T>
auto componentStorage() -> ComponentStorage
{
    ComponentStorage storage;
    if constexpr (TagComponent<T>)
    {
        static T tag{};
        storage.find = [](Registry& registry, Entity entity) -> void* { return registry.has<T>(entity) ? &tag : nullptr; };
        storage.add = [](Registry& registry, Entity entity, void*) {
            if (!registry.has<T>(entity))
                registry.add<T>(entity);
        };
    }
    else
    {
        storage.find = [](Registry& registry, Entity entity) -> void* { return registry.has<T>(entity) ? &registry.get<T>(entity) : nullptr; };
        storage.add = [](Registry& registry, Entity entity, void* value) {
            auto& component = *static_cast<T*>(value);
            if (registry.has<T>(entity))
                registry.get<T>(entity) = std::move(component);
            else
                registry.add<T>(entity, std::move(component));
        };
    }

    if constexpr (IsRequiredComponent<T>)
    {
        storage.remove = [](Registry&, Entity) { return false; };
    }
    else
    {
        storage.remove = [](Registry& registry, Entity entity) {
            if (!registry.has<T>(entity))
                return false;
            registry.remove<T>(entity);
            return true;
        };
    }
    storage.changed = changedHook<T>();
    storage.first = [](Registry& registry) { return registry.first<T>(); };
    return storage;
}

/// @brief Registers T as a component named `name` (see reflect::TypeRegistry::component) and attaches its
///        ComponentStorage, so a scene file can save and load it. Add the persistent fields and the editor
///        metadata to the builder it returns; a required component (ComponentTraits) is marked required. A game
///        registers its own components the same way.
template<typename T>
[[nodiscard]] auto registerComponent(reflect::TypeRegistry& types, std::string_view name) -> std::expected<reflect::TypeBuilder<T>, reflect::Error>
{
    auto builder = types.component<T>(name);
    if (builder)
    {
        builder->attach(componentStorage<T>());
        if constexpr (IsRequiredComponent<T>)
            builder->required();
    }
    return builder;
}

/// @brief Like registerComponent, for a component a world holds one of at most (ComponentStorage::singleton)
template<typename T>
[[nodiscard]] auto registerSingleton(reflect::TypeRegistry& types, std::string_view name) -> std::expected<reflect::TypeBuilder<T>, reflect::Error>
{
    auto builder = registerComponent<T>(types, name);
    if (builder)
    {
        auto storage = componentStorage<T>();
        storage.singleton = true;
        builder->attach(std::move(storage));
    }
    return builder;
}

/// @brief Registers the built-in components a scene file holds, under their C++ names, with their editor metadata
/// @note Not registered, because the file holds them another way or they are rebuilt at run time: Name and
///       StableId (an entity's "name" and "id"), Parent, Siblings and Children (its "children"),
///       GlobalTransform and StaticDirtyTag (computed by the TransformSystem), and Scripts (scripts are
///       registered types of their own). Camera saves its lens; its aspect and matrices are recomputed.
auto registerComponents(reflect::TypeRegistry& types) -> std::expected<void, reflect::Error>;
}
