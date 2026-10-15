module;

#include <concepts>

export module aegis.scene.system;

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
};

template<typename T>
concept SystemDerived = std::derived_from<T, System>;
}