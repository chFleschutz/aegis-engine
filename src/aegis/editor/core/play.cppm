export module aegis.editor.core:play;
import std;
import aegis.reflect;
import aegis.world;
import :inspector;

export namespace aegis::editor
{
/// @brief Copies every entity of 'from' into 'to', an empty registry, through the serializer (serialize::saveScene,
///        then serialize::instantiate): the play world's copy of the edit world. The entities keep their StableIds,
///        so a selection holds in both; the entities of scene instances are recreated by their holders, with 'prepare'
///        finishing each component (the asset library resolves its references). 'from' is only read.
/// @return Why the world could not be copied; 'to' is then unchanged.
[[nodiscard]] auto copyWorld(world::Registry& from, world::Registry& to, const reflect::TypeRegistry& types, const PrepareField& prepare = {})
    -> std::expected<void, std::string>;

/// @brief Whether the play world runs: Play starts it, Pause and Step hold it and run one frame, Stop ends it. Pure
///        state; the editor shell creates and destroys the play world.
class PlaySession
{
public:
    enum class State
    {
        Stopped, ///< Editing: there is no play world
        Playing,
        Paused,
    };

    [[nodiscard]] auto state() const -> State { return m_state; }
    /// @brief Whether there is a play world (playing or paused).
    [[nodiscard]] auto active() const -> bool { return m_state != State::Stopped; }

    /// @brief Starts playing; from Paused, resumes.
    void play();
    /// @brief Holds the play world; nothing while stopped.
    void pause();
    /// @brief Runs one frame of a paused play world (the next shouldUpdate); nothing unless paused.
    void step();
    /// @brief Ends the play session.
    void stop();

    /// @brief Whether the play world updates this frame: while playing, and once after step(). Asked once per frame.
    [[nodiscard]] auto shouldUpdate() -> bool;

private:
    State m_state{ State::Stopped };
    bool m_step{ false };
};
}
