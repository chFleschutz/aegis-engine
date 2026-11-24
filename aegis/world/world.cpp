module aegis.world;

import std;
import aegis.core;

namespace aegis::world
{
void World::setMainCamera(Entity camera)
{
    std::vector<Entity> previous;
    for (const auto entity : m_registry.view<MainCamera>())
    {
        if (Entity{ entity } != camera)
            previous.push_back(Entity{ entity });
    }
    for (const auto entity : previous)
        m_registry.remove<MainCamera>(entity);
    if (m_registry.valid(camera) && !m_registry.has<MainCamera>(camera))
        m_registry.add<MainCamera>(camera);
}

void World::begin(UpdateMode mode)
{
    for (auto& system : m_systems)
    {
        if (runs(*system, mode))
            system->onBegin(m_registry);
    }
}

void World::update(float deltaSeconds, UpdateMode mode)
{
    aegis::ScopeProfiler profiler("Scene Update");

    applyDeferred();

    auto& time = m_registry.m_time;
    time.deltaSeconds = deltaSeconds;
    time.elapsedSeconds += deltaSeconds;
    ++time.frame;
    time.fixedStepSeconds = m_fixedStep.step();
    const auto steps = mode == UpdateMode::Play ? m_fixedStep.advance(deltaSeconds) : 0;
    time.alpha = m_fixedStep.alpha();

    for (const auto phase : { Phase::PreUpdate, Phase::FixedUpdate, Phase::Update, Phase::PostUpdate, Phase::LateUpdate })
    {
        if (phase == Phase::FixedUpdate)
        {
            for (std::uint32_t step = 0; step < steps; ++step)
            {
                ++time.steps;
                for (auto& system : m_systems)
                    system->onFixedUpdate(m_registry, time.fixedStepSeconds);
            }
        }
        for (auto& system : m_systems)
        {
            if (system->phase() == phase && runs(*system, mode))
                system->onUpdate(m_registry, deltaSeconds);
        }
    }
}

void World::reset()
{
    for (auto& system : m_systems)
    {
        system->onReset(m_registry);
    }
    m_deferred.clear();
    m_registry.clear();
    for (auto& system : m_systems)
        system->onDetach(m_registry);
    m_systems.clear();
}

void World::applyDeferred()
{
    // A command may defer more; those wait for the next update
    auto commands = std::move(m_deferred);
    m_deferred.clear();
    for (auto& command : commands)
        command(m_registry);
}
}
