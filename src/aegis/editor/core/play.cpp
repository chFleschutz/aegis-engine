module aegis.editor.core;
import std;
import aegis.reflect;
import aegis.serialize;
import aegis.world;

namespace aegis::editor
{
auto copyWorld(world::Registry& from, world::Registry& to, const reflect::TypeRegistry& types, const PrepareField& prepare)
    -> std::expected<void, std::string>
{
    // The copy is never written: its ID names no file.
    const auto scene = serialize::saveScene(from, types, 0);
    if (!scene)
        return std::unexpected{ std::format("Cannot capture the world: {}", serialize::describe(scene.error())) };
    if (auto created = serialize::instantiate(to, types, *scene, prepare); !created)
        return std::unexpected{ std::format("Cannot copy the world: {}", serialize::describe(created.error())) };
    return {};
}

void PlaySession::play()
{
    m_state = State::Playing;
    m_step = false;
}

void PlaySession::pause()
{
    if (m_state == State::Playing)
        m_state = State::Paused;
}

void PlaySession::step()
{
    if (m_state == State::Paused)
        m_step = true;
}

void PlaySession::stop()
{
    m_state = State::Stopped;
    m_step = false;
}

auto PlaySession::shouldUpdate() -> bool
{
    if (m_state == State::Playing)
        return true;
    return std::exchange(m_step, false);
}
}
