module;

#include "core/assert.h"

export module aegis.scripting.script;

import std;
import aegis.platform;
import aegis.reflect;
import aegis.scene.registry;

export namespace aegis::scripting
{
/// @brief Everything a script can reach: its entity, the entity's registry and the input
///
/// This is deliberately narrow. A script gets no engine, renderer, window or asset access: what it
/// needs beyond components and input is handed to it through its own registered fields.
class ScriptContext
{
public:
    /// @param input Null for a world without a window (tests, a server); input() then asserts
    ScriptContext(scene::Registry& registry, scene::Entity entity, platform::Input* input) :
        m_registry(registry),
        m_entity(entity),
        m_input(input)
    {
    }

    /// @brief The entity the script is attached to
    [[nodiscard]] auto entity() const -> scene::Entity { return m_entity; }
    /// @brief The registry of the entity's world: spawn, destroy and find other entities here
    [[nodiscard]] auto registry() const -> scene::Registry& { return m_registry; }

    /// @brief The keyboard and mouse input
    [[nodiscard]] auto input() const -> platform::Input&
    {
        AGX_ASSERT_X(m_input, "This world has no input");
        return *m_input;
    }

    /// @brief Returns true if the entity has all components of type T...
    template<typename... T>
    [[nodiscard]] auto has() const -> bool
    {
        return m_registry.has<T...>(m_entity);
    }

    /// @brief Returns a reference to the entity's component of type T (it must have one)
    template<typename T>
    [[nodiscard]] auto get() const -> T&
    {
        return m_registry.get<T>(m_entity);
    }

    /// @brief Adds a component of type T to the entity and returns a reference to it
    template<typename T, typename... Args>
    auto add(Args&&... args) const -> T&
    {
        return m_registry.add<T>(m_entity, std::forward<Args>(args)...);
    }

private:
    scene::Registry& m_registry;
    scene::Entity m_entity;
    platform::Input* m_input;
};

/// @brief Behavior attached to an entity, with the fixed hooks onCreate, onUpdate and onDestroy
///
/// A script is created by its registered name (reflect::TypeRegistry::script), so its persistent state
/// belongs in registered fields: that is what a scene file saves and the editor shows. Anything else a
/// script keeps (cached values, timers) is runtime state and is rebuilt in onCreate.
/// Register it with registerScript, which records the Script base the system needs.
class Script
{
public:
    Script() = default;
    Script(const Script&) = delete;
    Script& operator=(const Script&) = delete;
    virtual ~Script() = default;

    /// @brief Called once, before the script's first onUpdate (the entity and its other components exist)
    virtual void onCreate(ScriptContext& context) {}
    /// @brief Called every frame with the time of the last frame in seconds
    virtual void onUpdate(ScriptContext& context, float deltaSeconds) {}
    /// @brief Called once when the entity is destroyed or the scene is reset, with the entity still intact
    /// @note Only for scripts that got onCreate
    virtual void onDestroy(ScriptContext& context) {}
};

/// @brief A script attached to an entity
struct ScriptEntry
{
    enum class State : std::uint8_t
    {
        Attached,  // onCreate not called yet
        Created,   // Running
        Destroyed, // onDestroy was called (or the script never ran)
    };

    std::string name; // Its registered name
    std::shared_ptr<Script> script;
    State state{ State::Attached };
};

/// @brief The scripts of an entity, in attach order. The ScriptSystem runs them.
/// @note The scripts end with the entity: Registry::destroy and Scene::reset call onDestroy first.
struct Scripts
{
    Scripts() = default;
    Scripts(const Scripts&) = delete;
    Scripts(Scripts&&) = default;
    auto operator=(const Scripts&) -> Scripts& = delete;
    auto operator=(Scripts&&) -> Scripts& = default;

    std::vector<ScriptEntry> entries;
};
}

export namespace aegis
{
template<>
struct ComponentTraits<scripting::Scripts>
{
    static constexpr bool is_required = true; // Removing it would skip onDestroy
};
}

export namespace aegis::scripting
{
/// @brief Registers T as a script named `name` (see reflect::TypeRegistry::script) and records its Script
///        base. Add the persistent fields to the builder it returns.
template<class T>
    requires std::derived_from<T, Script>
[[nodiscard]] auto registerScript(reflect::TypeRegistry& types, std::string_view name) -> std::expected<reflect::TypeBuilder<T>, reflect::Error>
{
    auto builder = types.script<T>(name);
    if (builder)
        builder->template base<Script>();
    return builder;
}

/// @brief Attaches a new script, created by its registered name, to the entity
/// @return The script (owned by the entity's Scripts component), or an error if the name is not
///         registered (UnknownType), is a component (WrongCategory) or cannot be created
/// @note onCreate runs when the ScriptSystem next updates (or at Scene::begin), not here
auto attachScript(scene::Registry& registry, const reflect::TypeRegistry& types, scene::Entity entity, std::string_view name)
    -> std::expected<Script*, reflect::Error>
{
    AGX_ASSERT_X(registry.valid(entity), "Cannot attach a script: Entity is not valid");

    const reflect::TypeInfo* info = types.find(name);
    if (!info)
        return reflect::makeError(reflect::ErrorCode::UnknownType, std::format("no script named '{}' is registered", name));
    if (info->category != reflect::TypeCategory::Script)
        return reflect::makeError(reflect::ErrorCode::WrongCategory, std::format("'{}' is a component, not a script", name));

    auto created = info->create();
    if (!created)
        return std::unexpected{ created.error() };

    Script* base = info->as<Script>(created->get());
    if (!base)
        return reflect::makeError(reflect::ErrorCode::WrongCategory, std::format("'{}' was not registered with registerScript", name));
    auto script = std::shared_ptr<Script>{ *created, base };
    Script* result = script.get();
    registry.getOrAdd<Scripts>(entity).entries.push_back({ .name = std::string{ name }, .script = std::move(script) });
    return result;
}
}
