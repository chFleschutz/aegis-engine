export module aegis.world.components;

import std;
export import aegis.math;
export import aegis.world.entity;
export import aegis.world.component_traits;

export namespace aegis
{
/// @brief Gives a name to the entity
struct Name
{
    std::string name = "Entity";
};

/// @brief The stable identity of an entity: a random 64-bit ID, unique within its world and never zero
/// @note entt::entity indices are reused after a destroy and can't be stored in a file, this ID can.
///       Registry::create assigns it, Registry::find looks an entity up by it, and it never changes.
struct StableId
{
    std::uint64_t value = 0;

    auto operator==(const StableId&) const -> bool = default;
};

/// @brief Stores the local transformation of the entity
struct Transform
{
    glm::vec3 location{ 0.0f, 0.0f, 0.0f };
    glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3 scale{ 1.0f, 1.0f, 1.0f };

    auto forward() const -> glm::vec3 { return math::forward(rotation); }
    auto right() const -> glm::vec3 { return math::right(rotation); }
    auto up() const -> glm::vec3 { return math::up(rotation); }
    auto matrix() const -> glm::mat4 { return math::tranformationMatrix(location, rotation, scale); }
};

/// @brief Transformation of the entity in world space (including parent transforms)
struct GlobalTransform
{
    glm::vec3 location{ 0.0f, 0.0f, 0.0f };
    glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3 scale{ 1.0f, 1.0f, 1.0f };

    auto forward() const -> glm::vec3 { return math::forward(rotation); }
    auto right() const -> glm::vec3 { return math::right(rotation); }
    auto up() const -> glm::vec3 { return math::up(rotation); }
    auto matrix() const -> glm::mat4 { return math::tranformationMatrix(location, rotation, scale); }
};

/// @brief Stores the parent entity
/// @note Use Entity::setParent to set the parent of an entity
struct Parent
{
    world::Entity entity{};
};

/// @brief Stores the next and previous sibling of the entity (for linked list)
/// @note Use Entity::addChild to add a child to an entity (siblings are automatically updated)
struct Siblings
{
    world::Entity next{};
    world::Entity prev{};
};

/// @brief Stores the children entities as the first and last child of a linked list
/// @note Use Entity::addChild to add a child to an entity
struct Children
{
    // TODO: This is currentl broken because the entity does not have a pointer to the scene anymore
    //struct Iterator
    //{
    //    world::Entity current{};

    //    auto operator*() const -> world::Entity { return current; }
    //    auto operator++() -> Iterator& { current = current.get<Siblings>().next; return *this; }
    //    auto operator==(const Iterator& other) const -> bool { return current == other.current; }
    //};

    //struct ReverseIterator
    //{
    //    world::Entity current{};

    //    auto operator*() const -> world::Entity { return current; }
    //    auto operator++() -> ReverseIterator& { current = current.get<Siblings>().prev; return *this; }
    //    auto operator==(const ReverseIterator& other) const -> bool { return current == other.current; };
    //};

    std::size_t count = 0;
    world::Entity first{};
    world::Entity last{};

    //auto begin() const -> Children::Iterator { return { first }; }
    //auto end() const -> Children::Iterator { return { world::Entity{} }; }
    //auto rbegin() const -> Children::ReverseIterator { return { last }; }
    //auto rend() const -> Children::ReverseIterator { return { world::Entity{} }; }
};

/// @brief Tags an entity as dynamic: its global transform is recomputed every frame.
///
/// The static/dynamic rule: an entity without this tag is static and stays frozen after World::begin,
/// so editing its Transform changes nothing on screen by itself.
///  - Anything that moves its entity every frame (a script, an animation) must tag it, and
///    only while the scene is set up: adding or removing the tag later is not supported yet.
///  - A one-off edit of a static entity (the editor's gizmo and inspector) calls
///    Registry::markStaticDirty(entity) after changing its Transform.
///  - Creating or destroying entities and adding or removing MeshRenderer need nothing: the
///    Registry's static-set version tracks them.
/// The tag does not pass down the hierarchy: tag each entity that moves on its own (descendants
/// of a moving entity follow it only if they are tagged too).
struct DynamicTag
{
};

/// @brief Marks a static entity whose global transform must be recomputed (Registry::markStaticDirty).
///        The TransformSystem recomputes it with its descendants and removes the tag.
struct StaticDirtyTag
{
};

struct AmbientLight
{
    glm::vec3 color = { 1.0f, 1.0f, 1.0f };
    float intensity = 0.1f;
};

struct DirectionalLight
{
    glm::vec3 color = { 1.0f, 1.0f, 1.0f };
    float intensity = 1.0f;
};

/// @brief Creates a light
struct PointLight
{
    glm::vec3 color = { 1.0f, 1.0f, 1.0f };
    float intensity = 100.0f;
};

/// @brief Tags the camera the scene is viewed through (the first entity with it and a Camera wins)
/// @note World::setMainCamera moves the tag, so one entity carries it
struct MainCamera
{
};

/// @brief Holds a camera to view the scene
struct Camera
{
    float fov = glm::radians(60.0f);
    float aspect = 1.0f;
    float near = 0.1f;
    float far = 1000.0f;

    glm::mat4 viewMatrix = glm::mat4{ 1.0f };
    glm::mat4 inverseViewMatrix = glm::mat4{ 1.0f };
    glm::mat4 projectionMatrix = glm::mat4{ 1.0f };
};

// Specialize component traits
template<>
struct ComponentTraits<Name>
{
    static constexpr bool is_required = true;
};

template<>
struct ComponentTraits<StableId>
{
    static constexpr bool is_required = true;
};

template<>
struct ComponentTraits<Transform>
{
    static constexpr bool is_required = true;
};

template<>
struct ComponentTraits<GlobalTransform>
{
    static constexpr bool is_required = true;
};

template<>
struct ComponentTraits<Parent>
{
    static constexpr bool is_required = true;
};

template<>
struct ComponentTraits<Siblings>
{
    static constexpr bool is_required = true;
};

template<>
struct ComponentTraits<Children>
{
    static constexpr bool is_required = true;
};
}
