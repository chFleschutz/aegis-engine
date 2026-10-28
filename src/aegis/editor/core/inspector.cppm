export module aegis.editor.core:inspector;
import std;
import aegis.reflect;
import aegis.world;
import aegis.world.types;

export namespace aegis::editor
{
/// @brief A registered component an entity has: its type and its address (see world::ComponentStorage::find).
struct ComponentSection
{
    const reflect::TypeInfo* type{ nullptr };
    void* object{ nullptr };
};

/// @brief The registered components 'entity' has, the required ones (Transform) first, the others in registration
///        order. Types registered without a ComponentStorage and scripts are left out.
[[nodiscard]] auto componentSections(const reflect::TypeRegistry& types, world::Registry& registry, world::Entity entity)
    -> std::vector<ComponentSection>;

/// @brief The components the Add Component menu offers for 'entity': the registered ones it does not have that can be
///        created, except required ones and singletons another entity already has. Sorted by category, then name.
[[nodiscard]] auto addableComponents(const reflect::TypeRegistry& types, world::Registry& registry, world::Entity entity)
    -> std::vector<const reflect::TypeInfo*>;

/// @brief The components the Create menu offers for a new entity: those of addableComponents, for an entity that has none.
[[nodiscard]] auto creatableComponents(const reflect::TypeRegistry& types, world::Registry& registry)
    -> std::vector<const reflect::TypeInfo*>;

/// @brief The scripts the Add Script menu offers: every registered script, sorted by category, then name.
[[nodiscard]] auto scriptTypes(const reflect::TypeRegistry& types) -> std::vector<const reflect::TypeInfo*>;

/// @brief Gives 'entity' a default instance of the component 'type'.
/// @return WrongCategory for a type without a ComponentStorage, NotConstructible for one that cannot be created
[[nodiscard]] auto addComponent(const reflect::TypeInfo& type, world::Registry& registry, world::Entity entity)
    -> std::expected<void, reflect::Error>;

/// @brief Removes the entity's component 'type'. False (and nothing removed) if it has none, the type is required or
///        has no ComponentStorage.
auto removeComponent(const reflect::TypeInfo& type, world::Registry& registry, world::Entity entity) -> bool;

/// @brief Finishes a written component before its change hook runs: the asset library loads a new asset reference
///        there (AssetLibrary::resolve).
using PrepareField = std::function<void(const reflect::TypeInfo& type, void* object)>;

/// @brief Writes 'value' to 'field' of 'object', the entity's component or script of 'type', runs 'prepare' on it,
///        then the component's change hook (world::ComponentStorage::changed), so the edit keeps the type's rules (D2).
/// @return The setter's error (a value of the wrong kind); nothing is written then
[[nodiscard]] auto setField(const reflect::TypeInfo& type, const reflect::FieldInfo& field, world::Registry& registry,
    world::Entity entity, void* object, const reflect::Value& value, const PrepareField& prepare = {}) -> std::expected<void, reflect::Error>;
}
