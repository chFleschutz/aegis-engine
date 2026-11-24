export module aegis.scripting:script;

import std;
import aegis.assets;
import aegis.platform;
import aegis.reflect;
import aegis.world;

export namespace aegis::scripting
{
/// @brief Instantiates scene and model assets for ScriptContext::spawn: the engine implements it over its asset
///        library and makes it each world's registry context (world::Registry::setContext<Spawner>), so
///        aegis.scripting needs no asset library
class Spawner
{
public:
    virtual ~Spawner() = default;

    /// @brief Creates an instance of the scene or model asset 'scene' in 'world', held by a new entity with the
    ///        StableId 'holderId', as a child of 'parent' (a root entity for a null 'parent')
    /// @return The holder, or why the scene could not be instantiated ('world' is then unchanged)
    [[nodiscard]] virtual auto spawn(world::World& world, assets::AssetId scene, world::Entity parent, std::uint64_t holderId)
        -> std::expected<world::Entity, std::string> = 0;
};

/// @brief Everything a script can reach: its entity, the entity's registry, the input, the world's time, spawning
///        and destroying entities, finding them by ID, and the world's events
///
/// This is deliberately narrow. A script gets no engine, renderer or window: what it needs beyond this is handed
/// to it through its own registered fields (the scene a spawner instantiates is an AssetRef field).
class ScriptContext
{
public:
    /// @param input This frame's input; build it with nothing down for a world without a window
    /// @param cursor Changes the cursor mode, or null for a world without a window (setCursorMode then does nothing)
    ScriptContext(world::Registry& registry, world::Entity entity, const platform::InputState& input, platform::Input* cursor = nullptr) :
        m_registry(registry),
        m_entity(entity),
        m_input(input),
        m_cursor(cursor)
    {
    }

    /// @brief The entity the script is attached to
    [[nodiscard]] auto entity() const -> world::Entity { return m_entity; }
    /// @brief The registry of the entity's world: spawn, destroy and find other entities here
    [[nodiscard]] auto registry() const -> world::Registry& { return m_registry; }

    /// @brief The keyboard and mouse input of this frame (empty while a UI has captured it)
    [[nodiscard]] auto input() const -> const platform::InputState& { return m_input; }

    /// @brief Shows, hides or captures the cursor; does nothing in a world without a window
    auto setCursorMode(platform::CursorMode mode) const -> void
    {
        if (m_cursor)
            m_cursor->setCursorMode(mode);
    }

    /// @brief The world's time as of this update: frame delta, fixed step, alpha, elapsed time and counters
    [[nodiscard]] auto time() const -> const world::Time& { return m_registry.time(); }

    /// @brief The entity with this StableId, or a null Entity
    [[nodiscard]] auto find(StableId id) const -> world::Entity { return m_registry.find(id); }

    /// @brief Instantiates the scene or model asset 'scene' at the start of the next update: a new entity holds the
    ///        instance, as a child of 'parent' (a root entity for a null 'parent'), with 'transform' as its local
    ///        transform
    /// @return The StableId the holder will have (find() it from the next update on, or send it in an event), or a
    ///         null StableId if the world cannot spawn (no World or no Spawner in the registry's context, or a
    ///         'parent' that is not alive)
    /// @note Spawning that fails when it runs (the scene doesn't load, 'parent' was destroyed meanwhile) logs a
    ///       warning and creates nothing.
    auto spawn(assets::AssetId scene, world::Entity parent = {}, const Transform& transform = {}) const -> StableId;

    /// @brief spawn() with the ID of an asset reference: a script's AssetRef field
    template<typename T>
    auto spawn(const assets::AssetRef<T>& scene, world::Entity parent = {}, const Transform& transform = {}) const -> StableId
    {
        return spawn(scene.id(), parent, transform);
    }

    /// @brief Destroys the entity and its descendants at the start of the next update (their scripts get onDestroy
    ///        then); an entity that is gone by then is skipped
    /// @note Does nothing, with a warning, in a registry without a World.
    auto destroy(world::Entity entity) const -> void;

    /// @brief Sends 'event' to every script and system of the world: they read it during the next update
    template<world::Event T>
    auto emit(T event) const -> void
    {
        m_registry.events().emit(std::move(event));
    }

    /// @brief The events of type T emitted during the previous update, in the order they were emitted
    template<world::Event T>
    [[nodiscard]] auto events() const -> std::span<const T>
    {
        return m_registry.events().read<T>();
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
    world::Registry& m_registry;
    world::Entity m_entity;
    const platform::InputState& m_input;
    platform::Input* m_cursor;
};

/// @brief Behavior attached to an entity, with the fixed hooks onCreate, onFixedUpdate, onUpdate and onDestroy
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

    /// @brief Called once, before the script's first onFixedUpdate or onUpdate (the entity and its other components exist)
    virtual void onCreate(ScriptContext& context) {}
    /// @brief Called once per fixed step of the world, zero or more times per frame and before onUpdate, with the
    ///        step's length in seconds: for physics and gameplay that must not depend on the frame rate
    /// @note context.time() holds the world's time, alpha included.
    virtual void onFixedUpdate(ScriptContext& context, float stepSeconds) {}
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
    void* object{ nullptr }; // The script as its registered type: the address its TypeInfo's fields take
    State state{ State::Attached };
};

/// @brief The scripts of an entity, in attach order. The ScriptSystem runs them.
/// @note The scripts end with the entity: Registry::destroy and World::reset call onDestroy first.
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
/// @note onCreate runs when the ScriptSystem next updates (or at World::begin), not here
auto attachScript(world::Registry& registry, const reflect::TypeRegistry& types, world::Entity entity, std::string_view name)
    -> std::expected<Script*, reflect::Error>;
}
