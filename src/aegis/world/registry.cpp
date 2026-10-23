module aegis.world.registry;

import std;

namespace aegis::world
{
auto Registry::createWithId(std::uint64_t id, const std::string& name, const glm::vec3& location, const glm::quat& rotation,
    const glm::vec3& scale) -> Entity
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

void Registry::destroy(Entity entity)
{
    for (const auto& listener : m_destroyListeners)
        listener.callback(entity);

    detach(entity);
    std::ignore = removeChildren(entity); // The entity is valid

    m_ids.erase(get<StableId>(entity).value);
    m_registry.destroy(entity.id());
}

void Registry::clear()
{
    m_registry.clear();
    m_ids.clear();
    invalidateStatic();
}

void Registry::addDestroyListener(const void* owner, std::function<void(Entity)> callback)
{
    m_destroyListeners.push_back({ owner, std::move(callback) });
}

void Registry::removeDestroyListener(const void* owner)
{
    std::erase_if(m_destroyListeners, [owner](const DestroyListener& listener) { return listener.owner == owner; });
}

auto Registry::find(StableId id) const -> Entity
{
    auto it = m_ids.find(id.value);
    return it == m_ids.end() ? Entity{} : Entity{ it->second };
}

auto Registry::removeParent(Entity entity) -> std::expected<void, HierarchyError>
{
    if (!valid(entity))
        return std::unexpected{ HierarchyError::InvalidEntity };

    detach(entity);
    return {};
}

auto Registry::isInSubtree(Entity entity, Entity root) const -> bool
{
    for (Entity current = entity; current; current = m_registry.get<Parent>(current.id()).entity)
    {
        if (current == root)
            return true;
    }
    return false;
}

auto Registry::addChild(Entity entity, Entity child) -> std::expected<void, HierarchyError>
{
    if (valid(entity) && valid(child) && get<Parent>(child).entity == entity)
        return {};

    return insertChild(entity, child, std::numeric_limits<std::size_t>::max());
}

auto Registry::insertChild(Entity entity, Entity child, std::size_t index) -> std::expected<void, HierarchyError>
{
    if (!valid(entity) || !valid(child))
        return std::unexpected{ HierarchyError::InvalidEntity };

    if (isInSubtree(entity, child)) // Would create a cycle
        return std::unexpected{ HierarchyError::Cycle };

    detach(child);

    auto& children = get<Children>(entity);
    Entity next{}; // The child that ends up behind the new one
    if (index < children.count)
    {
        next = children.first;
        for (std::size_t i = 0; i < index; ++i)
            next = get<Siblings>(next).next;
    }
    const Entity prev = next ? get<Siblings>(next).prev : children.last;

    get<Parent>(child).entity = entity;
    get<Siblings>(child) = Siblings{ .next = next, .prev = prev };
    if (prev)
        get<Siblings>(prev).next = child;
    else
        children.first = child;
    if (next)
        get<Siblings>(next).prev = child;
    else
        children.last = child;
    children.count++;
    return {};
}

auto Registry::removeChild(Entity entity, Entity child) -> std::expected<void, HierarchyError>
{
    if (!valid(entity) || !valid(child))
        return std::unexpected{ HierarchyError::InvalidEntity };
    if (get<Parent>(child).entity != entity)
        return std::unexpected{ HierarchyError::NotChild };

    detach(child);
    return {};
}

auto Registry::removeChildren(Entity entity) -> std::expected<void, HierarchyError>
{
    if (!valid(entity))
        return std::unexpected{ HierarchyError::InvalidEntity };

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
    return {};
}

auto Registry::markStaticDirty(Entity entity) -> void
{
    if (!valid(entity))
        return;

    m_registry.get_or_emplace<StaticDirtyTag>(entity.id());
    invalidateStatic();
}

void Registry::detach(Entity child)
{
    const Entity parent = get<Parent>(child).entity;
    if (!parent)
        return;

    auto& children = get<Children>(parent);
    children.count--;

    auto& siblings = get<Siblings>(child);
    const Entity prevSibling = siblings.prev;
    const Entity nextSibling = siblings.next;

    // Update siblings and first/last child
    if (prevSibling)
        get<Siblings>(prevSibling).next = nextSibling;
    else // First Child
        children.first = nextSibling;

    if (nextSibling)
        get<Siblings>(nextSibling).prev = prevSibling;
    else // Last Child
        children.last = prevSibling;

    get<Parent>(child) = Parent{};
    get<Siblings>(child) = Siblings{};
}

auto Registry::makeIdGenerator() -> std::mt19937_64
{
    std::random_device device;
    std::seed_seq seed{ device(), device(), device(), device() };
    return std::mt19937_64{ seed };
}

auto Registry::randomId() -> std::uint64_t
{
    std::uint64_t id = 0;
    while (id == 0 || m_ids.contains(id))
        id = m_idGenerator();
    return id;
}
}
