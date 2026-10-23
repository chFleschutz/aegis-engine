module aegis.editor.core;
import std;
import aegis.reflect;
import aegis.world;
import aegis.world.types;

namespace aegis::editor
{
namespace
{
auto byCategoryThenName(const reflect::TypeInfo* a, const reflect::TypeInfo* b) -> bool
{
    return std::tuple{ std::string_view{ a->meta.category }, a->displayName() } < std::tuple{ std::string_view{ b->meta.category }, b->displayName() };
}
}

auto componentSections(const reflect::TypeRegistry& types, world::Registry& registry, world::Entity entity)
    -> std::vector<ComponentSection>
{
    std::vector<ComponentSection> sections;
    for (const auto* type : types.types())
    {
        const auto* storage = type->attachment<world::ComponentStorage>();
        if (type->category != reflect::TypeCategory::Component || !storage)
            continue;
        if (void* object = storage->find(registry, entity))
            sections.push_back({ .type = type, .object = object });
    }
    std::ranges::stable_partition(sections, [](const ComponentSection& section) { return section.type->meta.required; });
    return sections;
}

auto addableComponents(const reflect::TypeRegistry& types, world::Registry& registry, world::Entity entity)
    -> std::vector<const reflect::TypeInfo*>
{
    std::vector<const reflect::TypeInfo*> addable;
    for (const auto* type : types.types())
    {
        const auto* storage = type->attachment<world::ComponentStorage>();
        if (type->category != reflect::TypeCategory::Component || !storage || type->meta.required || !type->construct)
            continue;
        if (storage->find(registry, entity))
            continue;
        if (storage->singleton && storage->first(registry))
            continue;
        addable.push_back(type);
    }
    std::ranges::stable_sort(addable, byCategoryThenName);
    return addable;
}

auto scriptTypes(const reflect::TypeRegistry& types) -> std::vector<const reflect::TypeInfo*>
{
    std::vector<const reflect::TypeInfo*> scripts;
    for (const auto* type : types.types())
    {
        if (type->category == reflect::TypeCategory::Script)
            scripts.push_back(type);
    }
    std::ranges::stable_sort(scripts, byCategoryThenName);
    return scripts;
}

auto addComponent(const reflect::TypeInfo& type, world::Registry& registry, world::Entity entity)
    -> std::expected<void, reflect::Error>
{
    const auto* storage = type.attachment<world::ComponentStorage>();
    if (type.category != reflect::TypeCategory::Component || !storage)
        return reflect::makeError(reflect::ErrorCode::WrongCategory, std::format("'{}' is not a component with storage", type.name));
    auto instance = type.create();
    if (!instance)
        return std::unexpected{ instance.error() };
    storage->add(registry, entity, instance->get());
    return {};
}

auto removeComponent(const reflect::TypeInfo& type, world::Registry& registry, world::Entity entity) -> bool
{
    const auto* storage = type.attachment<world::ComponentStorage>();
    return storage && storage->remove(registry, entity);
}

auto setField(const reflect::TypeInfo& type, const reflect::FieldInfo& field, world::Registry& registry,
    world::Entity entity, void* object, const reflect::Value& value, const PrepareField& prepare) -> std::expected<void, reflect::Error>
{
    if (auto written = field.setter(object, value); !written)
        return written;
    if (prepare)
        prepare(type, object);
    if (const auto* storage = type.attachment<world::ComponentStorage>())
        storage->changed(registry, entity);
    return {};
}
}
