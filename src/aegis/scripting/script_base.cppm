export module aegis.scripting.script_base;

import aegis.scene.entity;
import aegis.scene.registry;
import aegis.platform;

export namespace aegis::scripting
{
class ScriptBase
{
public:
    /// @brief Constructor
    /// @note When overriding, dont't use member functions (m_entity is not initialized yet)
    ScriptBase() = default;
    virtual ~ScriptBase() = default;

    void init(scene::Registry* reg, scene::Entity entity, aegis::platform::Input& input)
    {
        m_registry = reg;
        m_entity = entity;
        m_input = &input;
    }

    /// @brief Called once at the first frame just before update
    virtual void begin() {}
    /// @brief Called every frame with the delta of the last two frames in seconds
    virtual void update(float deltaSeconds) {}
    /// @brief Called once at the end of the last frame
    virtual void end() {}

    /// @brief Returns true if the entity has all components of type T...
    template<typename... T>
    auto has() const -> bool
    {
        return m_registry->has<T...>(m_entity);
    }

    /// @brief Returns a reference of the component of type T
    /// @note Component of type T must exist
    template<typename T>
    auto get() const -> T&
    {
        return m_registry->get<T>(m_entity);
    }

    /// @brief Adds a component of type T to the entity and returns a reference to it
    //template<typename Component, typename... Args>
    //auto add(Args&&... args) -> Component&
    //{
    //    return m_registry->add<Component>(m_entity, std::forward<Args>(args)...);
    //}

    /// @brief Returns the entity
    auto entity() const -> scene::Entity { return m_entity; }

    /// @brief Returns the engine's keyboard and mouse input
    auto input() const -> aegis::platform::Input& { return *m_input; }

private:
    scene::Registry* m_registry{ nullptr };
    scene::Entity m_entity;
    aegis::platform::Input* m_input{ nullptr };
};
}
