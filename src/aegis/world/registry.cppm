module;

#include <cassert>

export module aegis.world.registry;

import std;
export import aegis.math;
export import aegis.world.entity;
export import aegis.world.components;

export namespace aegis::world
{
/// @brief Why a hierarchy change (Registry::addChild, insertChild, ...) was refused
enum class HierarchyError
{
    InvalidEntity, ///< An entity is null or destroyed
    NotChild,      ///< The entity is not a child of the given parent
    Cycle,         ///< The new parent is the entity itself or one of its descendants
};

[[nodiscard]] constexpr auto describe(HierarchyError error) -> std::string_view
{
    switch (error)
    {
    case HierarchyError::InvalidEntity: return "an entity is null or destroyed";
    case HierarchyError::NotChild: return "the entity is not a child of the parent";
    case HierarchyError::Cycle: return "the parent is the entity or one of its descendants";
    }
    return "unknown hierarchy error";
}

class Registry
{
public:
    Registry() = default;
    Registry(const Registry&) = delete;
    Registry(Registry&&) = delete;

    auto operator=(const Registry&) -> Registry& = delete;
    auto operator=(Registry&&) -> Registry& = delete;

    /// @brief Number of live entities
    /// @note The entity storage keeps destroyed ids for reuse, its free list marks where the live ones end
    [[nodiscard]] auto entityCount() const -> std::size_t { return m_registry.storage<entt::entity>()->free_list(); }

    /// @brief Creates an entity with a StableId (random, unique in this registry), a Name and a Transform
    /// @note world::Entity can be passed by value
    auto create(const std::string& name = std::string{}, const glm::vec3& location = glm::vec3{ 0.0f },
        const glm::quat& rotation = glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f }, const glm::vec3& scale = glm::vec3{ 1.0f }) -> Entity
    {
        return createWithId(randomId(), name, location, rotation, scale);
    }

    /// @brief Like create, with a given StableId (a loader restoring a saved entity)
    /// @return The new entity, or a null Entity (and nothing created) if the id is zero or already in use
    auto createWithId(std::uint64_t id, const std::string& name = std::string{}, const glm::vec3& location = glm::vec3{ 0.0f },
        const glm::quat& rotation = glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f }, const glm::vec3& scale = glm::vec3{ 1.0f }) -> Entity;

    /// @brief Destroys the entity (its StableId is gone with it and is not reused)
    /// @note The listeners added with addDestroyListener run first, while the entity still has all its components
    void destroy(Entity entity);

    /// @brief Destroys every entity, without calling the destroy listeners (end scripts and the like first)
    /// @note The StableIds are released; ids are random, so a new entity is unlikely to get a cleared one
    void clear();

    /// @brief Calls callback(entity) at the start of every destroy(), before the entity loses anything
    /// @param owner Identifies the listener for removeDestroyListener
    void addDestroyListener(const void* owner, std::function<void(Entity)> callback);

    void removeDestroyListener(const void* owner);

    /// @brief The entity with this StableId, or a null Entity
    [[nodiscard]] auto find(StableId id) const -> Entity;

    /// @brief The first entity that has all components of type T... (a null Entity if none does)
    /// @note This is how the scene's singletons are found: there is no side table that could dangle
    template<typename... T>
    [[nodiscard]] auto first() const -> Entity
    {
        const auto view = std::as_const(m_registry).template view<const T...>();
        const auto it = view.begin();
        return it == view.end() ? Entity{} : Entity{ *it };
    }

    /// @brief Checks that the entity is alive: not null, not destroyed (also when its id was recycled)
    [[nodiscard]] auto valid(Entity entity) const -> bool { return entity && m_registry.valid(entity.id()); }

    /// @brief Checks if the entity has all components of type T...
    template<typename... T>
    auto has(Entity entity) const -> bool
    {
        return m_registry.all_of<T...>(entity.id());
    }

    /// @brief Acces to the component of type T
    template<typename T>
    auto get(Entity entity) -> T&
    {
        assert(has<T>(entity) && "Cannot get Component: Entity does not have the component");
        return m_registry.get<T>(entity.id());
    }

    /// @brief Adds a component of type T to the entity
    /// @return A refrence to the new component
    template<typename T, typename... Args>
    auto add(Entity entity, Args&&... args) -> T&
    {
        assert(!has<T>(entity) && "Cannot add Component: Entity already has the component");
        return m_registry.emplace<T>(entity.id(), std::forward<Args>(args)...);
    }

    /// @brief Overload to add tag components (empty structs) to the entity
    template<TagComponent T>
    void add(Entity entity)
    {
        assert(!has<T>(entity) && "Cannot add Component: Entity already has the component");
        m_registry.emplace<T>(entity.id());
    }

    template<typename T>
    auto getOrAdd(Entity entity) -> T&
    {
        return m_registry.get_or_emplace<T>(entity.id());
    }

    /// @brief Removes a component of type T from the entity
    /// @note Entity MUST have the component and it MUST be an optional component
    template<typename T>
        requires IsOptionalComponent<T>
    void remove(Entity entity)
    {
        assert(has<T>(entity) && "Cannot remove Component: Entity does not have the component");
        m_registry.remove<T>(entity.id());
    }

    template<typename... Components, typename... Exclude>
    auto view(entt::exclude_t<Exclude...> exclusion = entt::exclude_t{})
    {
        return m_registry.view<Components...>(exclusion);
    }

    /// @brief Makes the entity the last child of parent, detaching it from its previous parent
    /// @return An error (and changes nothing) if either entity is null or destroyed, or parent is the entity
    ///         itself or one of its descendants
    auto setParent(Entity entity, Entity parent) -> std::expected<void, HierarchyError> { return addChild(parent, entity); }

    /// @brief Detaches the entity from its parent (nothing happens if it has none)
    auto removeParent(Entity entity) -> std::expected<void, HierarchyError>;

    /// @brief Checks if the entity is root or one of root's descendants
    [[nodiscard]] auto isInSubtree(Entity entity, Entity root) const -> bool;

    /// @brief Makes child the last child of the entity, detaching it from its previous parent
    /// @note A child of the entity already keeps its place
    /// @return An error (and changes nothing) if either entity is null or destroyed, or the entity is child
    ///         itself or one of its descendants
    auto addChild(Entity entity, Entity child) -> std::expected<void, HierarchyError>;

    /// @brief Makes child the child of the entity at index, detaching it from its previous parent
    /// @param index The child's position among the entity's children afterwards; past the end it becomes the last
    /// @note A child of the entity is moved to the index (counted without it)
    /// @return An error (and changes nothing) if either entity is null or destroyed, or the entity is child
    ///         itself or one of its descendants
    auto insertChild(Entity entity, Entity child, std::size_t index) -> std::expected<void, HierarchyError>;

    /// @return An error (and changes nothing) if either entity is null or destroyed, or child is not a child of the entity
    auto removeChild(Entity entity, Entity child) -> std::expected<void, HierarchyError>;

    /// @brief Detaches every child of the entity
    auto removeChildren(Entity entity) -> std::expected<void, HierarchyError>;

    /// @brief Counts the changes to the set of static entities: it grows with every invalidateStatic()
    ///        and markStaticDirty(). Consumers that cache the static set (the GPU-driven instance buffers)
    ///        rebuild it when the version differs from the one they built.
    [[nodiscard]] auto staticVersion() const noexcept -> std::uint64_t { return m_staticVersion; }

    /// @brief The static set changed (an instance came or went, or moved between static and dynamic).
    auto invalidateStatic() noexcept -> void { ++m_staticVersion; }

    /// @brief A static entity's Transform (or its ancestors') was edited. Its global transform and its
    ///        descendants' are recomputed by the TransformSystem, and the static set is invalidated.
    auto markStaticDirty(Entity entity) -> void;

    template<typename Component>
    auto onConstruct()
    {
        return m_registry.on_construct<Component>();
    }

    template<typename Component>
    auto onDestroy()
    {
        return m_registry.on_destroy<Component>();
    }

private:
    /// @brief Unlinks the (valid) child from its parent's children, if it has a parent
    void detach(Entity child);

    struct DestroyListener
    {
        const void* owner;
        std::function<void(Entity)> callback;
    };

    static auto makeIdGenerator() -> std::mt19937_64;

    auto randomId() -> std::uint64_t;

    entt::registry m_registry;
    std::unordered_map<std::uint64_t, entt::entity> m_ids;
    std::mt19937_64 m_idGenerator{ makeIdGenerator() };
    std::vector<DestroyListener> m_destroyListeners;
    std::uint64_t m_staticVersion{ 0 };
};
}