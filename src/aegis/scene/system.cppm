export module aegis.scene.system;

import std;
export import aegis.scene.registry;

export namespace aegis::scene
{
class System
{
public:
    System() = default;
    virtual ~System() = default;

    virtual void onAttach() {}
    virtual void onDetach() {}
    virtual void onBegin(Registry& registry) {}
    virtual void onUpdate(Registry& registry, float deltaSeconds) {}
    /// @brief The scene is about to be cleared: every entity still exists with all its components,
    ///        so a system can end what it runs on them (Scene::reset, Scene's destructor)
    virtual void onReset(Registry& registry) {}
};

template<typename T>
concept SystemDerived = std::derived_from<T, System>;
}