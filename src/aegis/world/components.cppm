export module aegis.world:components;

import std;
export import aegis.math;
import :entity;

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
    auto matrix() const -> glm::mat4 { return math::transformMatrix(location, rotation, scale); }
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
    auto matrix() const -> glm::mat4 { return math::transformMatrix(location, rotation, scale); }
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
    std::size_t count = 0;
    world::Entity first{};
    world::Entity last{};
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

/// @brief Light from infinitely far away (the sun) in the direction of the entity's transform's forward: it shines
///        along -forward. Shadowed near the camera
struct DirectionalLight
{
    glm::vec3 color = { 1.0f, 1.0f, 1.0f };
    float intensity = 1.0f;
    float shadowDistance = 50.0f;     // Meters from the camera that its shadow covers
    float shadowBias = 1.0f;          // Shadow-map texels a shaded point moves along its normal against self-shadowing
    std::uint32_t shadowCascades = 4; // Shadow maps the shadow distance is split into, 1 to 4
    float shadowSplitLambda = 0.75f;  // How the cascades split the distance: 1 logarithmic, 0 uniform
};

/// @brief Light shining from the entity's location in all directions
struct PointLight
{
    glm::vec3 color = { 1.0f, 1.0f, 1.0f };
    float intensity = 100.0f;
    float range = 20.0f; // Meters; the light fades to zero there
};

/// @brief Light shining from the entity's location in a cone along its transform's forward
struct SpotLight
{
    glm::vec3 color = { 1.0f, 1.0f, 1.0f };
    float intensity = 100.0f;
    float range = 20.0f;                        // Meters; the light fades to zero there
    float innerConeAngle = 0.0f;                // Radians from the axis where the light starts to fade
    float outerConeAngle = glm::radians(45.0f); // Radians from the axis where it reaches zero
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

/// @brief How PostProcess maps the frame's HDR colors to the screen
enum class ToneMapping
{
    Reinhard,
    Aces,
};

/// @brief How the frame seen through a camera is finished: tone mapping, exposure, gamma and bloom. Read from the
///        main camera's entity; a main camera without one renders with these defaults.
struct PostProcess
{
    ToneMapping toneMapping = ToneMapping::Aces;
    float exposure = 1.0f;
    float gamma = 2.2f;
    float bloomIntensity = 0.1f;
    float bloomThreshold = 0.0f;     // The brightness bloom starts at
    float bloomSoftThreshold = 0.5f; // How gradually it starts (0: a hard cut)
};

/// @brief A component of the entity's own bookkeeping: Registry::create gives every entity one and the registry
///        relies on it until the entity is destroyed, so Registry::remove refuses it at compile time. Whether the
///        editor may remove a registered component is its metadata's say instead (reflect::TypeMeta::required).
template<typename T>
concept EntityComponent = std::same_as<T, Name> || std::same_as<T, StableId> || std::same_as<T, Transform> ||
                          std::same_as<T, GlobalTransform> || std::same_as<T, Parent> || std::same_as<T, Siblings> ||
                          std::same_as<T, Children>;

/// @brief A component with no data (a tag): it has no storage, only presence
template<typename T>
concept TagComponent = std::is_empty_v<T>;
}
