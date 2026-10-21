export module aegis.world.types;

import std;
import aegis.assets;
import aegis.reflect;
import aegis.world.registry;
import aegis.world.render_components;

export namespace aegis::world
{
/// @brief Reaches a registered component on an entity without naming its type, for code that works only
///        through the type registry (the scene serializer). registerComponent attaches it to the type.
/// @note A tag (an empty type) has no storage: find's pointer is to a shared dummy, and it has no fields.
struct ComponentStorage
{
    /// @brief The entity's component, or null if it has none
    std::function<void*(Registry&, Entity)> find;
    /// @brief Gives the entity 'value' (an instance of the type, moved from), replacing the component it has.
    ///        Build the value first: observers of the component's construction (the draw batches) see it as it
    ///        is added.
    std::function<void(Registry&, Entity, void* value)> add;
};

/// @brief The ComponentStorage of T
template<typename T>
auto componentStorage() -> ComponentStorage
{
    if constexpr (TagComponent<T>)
    {
        static T tag{};
        return ComponentStorage{
            .find = [](Registry& registry, Entity entity) -> void* { return registry.has<T>(entity) ? &tag : nullptr; },
            .add = [](Registry& registry, Entity entity, void*) {
                if (!registry.has<T>(entity))
                    registry.add<T>(entity); },
        };
    }
    else
    {
        return ComponentStorage{
            .find = [](Registry& registry, Entity entity) -> void* { return registry.has<T>(entity) ? &registry.get<T>(entity) : nullptr; },
            .add = [](Registry& registry, Entity entity, void* value) {
                auto& component = *static_cast<T*>(value);
                if (registry.has<T>(entity))
                    registry.get<T>(entity) = std::move(component);
                else
                    registry.add<T>(entity, std::move(component)); },
        };
    }
}

/// @brief Registers T as a component named `name` (see reflect::TypeRegistry::component) and attaches its
///        ComponentStorage, so a scene file can save and load it. Add the persistent fields to the builder
///        it returns. A game registers its own components the same way.
template<typename T>
[[nodiscard]] auto registerComponent(reflect::TypeRegistry& types, std::string_view name) -> std::expected<reflect::TypeBuilder<T>, reflect::Error>
{
    auto builder = types.component<T>(name);
    if (builder)
        builder->attach(componentStorage<T>());
    return builder;
}

/// @brief Registers the built-in components a scene file holds, under their C++ names
/// @note Not registered, because the file holds them another way or they are rebuilt at run time: Name and
///       StableId (an entity's "name" and "id"), Parent, Siblings and Children (its "children"),
///       GlobalTransform and StaticDirtyTag (computed by the TransformSystem), and Scripts (scripts are
///       registered types of their own). Camera saves its lens; its aspect and matrices are recomputed.
auto registerComponents(reflect::TypeRegistry& types) -> std::expected<void, reflect::Error>
{
    // The first failure: a later registration is skipped once one fails
    std::expected<void, reflect::Error> status;
    auto check = [&](const auto& builder) {
        if (!status)
            return;
        if (!builder)
            status = std::unexpected{ builder.error() };
        else if (!builder->status())
            status = builder->status();
    };

    {
        auto builder = registerComponent<Transform>(types, "Transform");
        if (builder)
            builder->field("location", &Transform::location).field("rotation", &Transform::rotation).field("scale", &Transform::scale);
        check(builder);
    }
    check(registerComponent<DynamicTag>(types, "DynamicTag"));
    check(registerComponent<MainCamera>(types, "MainCamera"));
    {
        auto builder = registerComponent<Camera>(types, "Camera");
        if (builder)
            builder->field("fov", &Camera::fov).field("near", &Camera::near).field("far", &Camera::far);
        check(builder);
    }
    {
        auto builder = registerComponent<AmbientLight>(types, "AmbientLight");
        if (builder)
            builder->field("color", &AmbientLight::color).field("intensity", &AmbientLight::intensity);
        check(builder);
    }
    {
        auto builder = registerComponent<DirectionalLight>(types, "DirectionalLight");
        if (builder)
            builder->field("color", &DirectionalLight::color).field("intensity", &DirectionalLight::intensity);
        check(builder);
    }
    {
        auto builder = registerComponent<PointLight>(types, "PointLight");
        if (builder)
            builder->field("color", &PointLight::color).field("intensity", &PointLight::intensity);
        check(builder);
    }
    {
        auto builder = registerComponent<MeshRenderer>(types, "MeshRenderer");
        if (builder)
            builder->field("mesh", &MeshRenderer::mesh).field("material", &MeshRenderer::material);
        check(builder);
    }
    {
        auto builder = registerComponent<Sky>(types, "Sky");
        if (builder)
            builder->field("environment", &Sky::environment);
        check(builder);
    }
    return status;
}
}
