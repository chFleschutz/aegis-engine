export module aegis.world.system;

import std;
export import aegis.world.registry;

export namespace aegis::world
{
/// @brief The part of the frame a system runs in. Phases run in this order; within one, systems run in
///        the order they were added.
enum class Phase
{
    PreUpdate,  ///< Before the game: input-driven work that gameplay builds on
    Update,     ///< Scripts and gameplay; the default
    PostUpdate, ///< After the game has written its changes: what derives from them (global transforms)
    LateUpdate  ///< After the derived data: what reads it (camera matrices)
};

/// @brief How a world runs: the game plays, or an editor edits it
enum class UpdateMode
{
    Play, ///< Every system runs: scripts, gameplay and what derives from them
    Edit  ///< Only the systems that opt in (runsInEdit) run, so entities move when edited and nothing plays
};

class System
{
public:
    System() = default;
    virtual ~System() = default;

    /// @brief The phase onUpdate runs in (onBegin and onReset don't depend on it)
    [[nodiscard]] virtual auto phase() const -> Phase { return Phase::Update; }
    /// @brief Whether onBegin and onUpdate also run in UpdateMode::Edit; scripts and gameplay don't
    [[nodiscard]] virtual auto runsInEdit() const -> bool { return false; }

    virtual void onAttach() {}
    virtual void onDetach() {}
    virtual void onBegin(Registry& registry) {}
    virtual void onUpdate(Registry& registry, float deltaSeconds) {}
    /// @brief The scene is about to be cleared: every entity still exists with all its components,
    ///        so a system can end what it runs on them (World::reset, World's destructor)
    virtual void onReset(Registry& registry) {}
};

template<typename T>
concept SystemDerived = std::derived_from<T, System>;
}