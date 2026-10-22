module;

#include <cassert>

export module aegis.world.registry;

import std;
export import aegis.math;
export import aegis.world.entity;
export import aegis.world.components;

export namespace aegis::world
{
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
        const glm::quat& rotation = glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f }, const glm::vec3& scale = glm::vec3{ 1.0f }) -> Entity
    {
        if (id == 0 || m_ids.contains(id))
            return Entity{};

        Entity entity{ m_registry.create() };
        m_ids.emplace(id, entity.id());
        add<StableId>(entity, id);
        add<Name>(entity, name.empty() ? "Entity" : name);
        add<Transform>(entity, location, rotation, scale);
        add<GlobalTransform>(entity);
        add<Parent>(entity);
        add<Siblings>(entity);
        add<Children>(entity);
        return entity;
    }

    /// @brief Destroys the entity (its StableId is gone with it and is not reused)
    /// @note The listeners added with addDestroyListener run first, while the entity still has all its components
    void destroy(Entity entity)
    {
        for (const auto& listener : m_destroyListeners)
            listener.callback(entity);

        removeParent(entity);
        removeChildren(entity);

        m_ids.erase(get<StableId>(entity).value);
        m_registry.destroy(entity.id());
    }

    /// @brief Destroys every entity, without calling the destroy listeners (end scripts and the like first)
    /// @note The StableIds are released; ids are random, so a new entity is unlikely to get a cleared one
    void clear()
    {
        m_registry.clear();
        m_ids.clear();
        invalidateStatic();
    }

    /// @brief Calls callback(entity) at the start of every destroy(), before the entity loses anything
    /// @param owner Identifies the listener for removeDestroyListener
    void addDestroyListener(const void* owner, std::function<void(Entity)> callback)
    {
        m_destroyListeners.push_back({ owner, std::move(callback) });
    }

    void removeDestroyListener(const void* owner)
    {
        std::erase_if(m_destroyListeners, [owner](const DestroyListener& listener) { return listener.owner == owner; });
    }

    /// @brief The entity with this StableId, or a null Entity
    [[nodiscard]] auto find(StableId id) const -> Entity
    {
        auto it = m_ids.find(id.value);
        return it == m_ids.end() ? Entity{} : Entity{ it->second };
    }

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
    /// @return False (and changes nothing) if parent is the entity itself or one of its descendants
    auto setParent(Entity entity, Entity parent) -> bool
    {
        assert(entity && "Cannot set parent: Entity is null");
        assert(parent && "Cannot set parent: Parent entity is null");

        return addChild(parent, entity);
    }

    void removeParent(Entity entity)
    {
        assert(entity && "Cannot remove parent: Entity is null");
        assert(has<Parent>(entity) && "Cannot remove parent: Entity does not have a parent");

        const Entity parent = get<Parent>(entity).entity;
        if (parent)
            removeChild(parent, entity);
    }

    /// @brief Checks if the entity is root or one of root's descendants
    [[nodiscard]] auto isInSubtree(Entity entity, Entity root) const -> bool
    {
        for (Entity current = entity; current; current = m_registry.get<Parent>(current.id()).entity)
        {
            if (current == root)
                return true;
        }
        return false;
    }

    /// @brief Makes child the last child of the entity, detaching it from its previous parent
    /// @return False (and changes nothing) if the entity is child itself or one of its descendants
    auto addChild(Entity entity, Entity child) -> bool
    {
        assert(entity && "Cannot add child: Entity is null");
        assert(child && "Cannot add child: Child entity is null");

        if (isInSubtree(entity, child)) // Would create a cycle
            return false;

        if (get<Parent>(child).entity == entity) // Already added as a child
            return true;

        removeParent(child);

        auto& children = get<Children>(entity);
        get<Parent>(child).entity = entity;
        get<Siblings>(child) = Siblings{ .next = Entity{}, .prev = children.last };
        if (children.last)
        {
            get<Siblings>(children.last).next = child;
        }
        else // No children yet
        {
            children.first = child;
        }
        children.last = child;
        children.count++;
        return true;
    }

    void removeChild(Entity entity, Entity child)
    {
        assert(entity && "Cannot remove child: Entity is null");
        assert(has<Children>(entity) && "Cannot remove child: Entity does not have children");
        assert(child && "Cannot remove child: Child entity is null");
        assert(has<Parent>(child) && "Cannot remove child: Entity does not have a parent");
        assert(get<Parent>(child).entity == entity && "Cannot remove child: Entity is not a child of this entity");

        auto& children = get<Children>(entity);
        children.count--;

        auto& siblings = get<Siblings>(child);
        Entity prevSibling = siblings.prev;
        Entity nextSibling = siblings.next;

        // Update siblings and first/last child
        if (prevSibling)
        {
            get<Siblings>(prevSibling).next = nextSibling;
        }
        else // First Child
        {
            children.first = siblings.next;
        }

        if (nextSibling)
        {
            get<Siblings>(nextSibling).prev = prevSibling;
        }
        else // Last Child
        {
            children.last = siblings.prev;
        }

        // Detach the child at the end
        get<Parent>(child) = Parent{};
        get<Siblings>(child) = Siblings{};
    }

    void removeChildren(Entity entity)
    {
        assert(entity && "Cannot remove children: Entity is null");
        assert(has<Children>(entity) && "Cannot remove children: Entity does not have children");

        // This needs to be done in two steps to avoid invalidating the iterator
        auto& children = get<Children>(entity);
        std::vector<Entity> childrenToRemove;
        childrenToRemove.reserve(children.count);

        // TODO: This can be simplified by repairing the iterators for the children
        Entity current = children.first;
        while (current)
        {
            childrenToRemove.emplace_back(current);
            current = get<Siblings>(current).next;
        }

        children = Children{};
        for (auto& child : childrenToRemove)
        {
            get<Parent>(child) = Parent{};
            get<Siblings>(child) = Siblings{};
        }
    }

    /// @brief Counts the changes to the set of static entities: it grows with every invalidateStatic()
    ///        and markStaticDirty(). Consumers that cache the static set (the GPU-driven instance buffers)
    ///        rebuild it when the version differs from the one they built.
    [[nodiscard]] auto staticVersion() const noexcept -> std::uint64_t { return m_staticVersion; }

    /// @brief The static set changed (an instance came or went, or moved between static and dynamic).
    auto invalidateStatic() noexcept -> void { ++m_staticVersion; }

    /// @brief A static entity's Transform (or its ancestors') was edited. Its global transform and its
    ///        descendants' are recomputed by the TransformSystem, and the static set is invalidated.
    auto markStaticDirty(Entity entity) -> void
    {
        if (!valid(entity))
            return;

        m_registry.get_or_emplace<StaticDirtyTag>(entity.id());
        invalidateStatic();
    }

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
    struct DestroyListener
    {
        const void* owner;
        std::function<void(Entity)> callback;
    };

    static auto makeIdGenerator() -> std::mt19937_64
    {
        std::random_device device;
        std::seed_seq seed{ device(), device(), device(), device() };
        return std::mt19937_64{ seed };
    }

    auto randomId() -> std::uint64_t
    {
        std::uint64_t id = 0;
        while (id == 0 || m_ids.contains(id))
            id = m_idGenerator();
        return id;
    }

    entt::registry m_registry;
    std::unordered_map<std::uint64_t, entt::entity> m_ids;
    std::mt19937_64 m_idGenerator{ makeIdGenerator() };
    std::vector<DestroyListener> m_destroyListeners;
    std::uint64_t m_staticVersion{ 0 };
};
}