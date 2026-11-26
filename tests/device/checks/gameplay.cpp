module aegis.test;

import std;
import aegis.core;
import aegis.engine;
import aegis.math;
import aegis.physics;
import aegis.platform;
import aegis.world;

#ifdef AEGIS_TEST_EXAMPLES
// The Examples game's module (AEGIS_GAME_MODULE in games/examples/game.cpp), linked in from Examples-module: the
// gameplay scene's scripts are the game's. It is defined outside any module, so it is declared there too.
extern "C++"
{
    namespace aegis::launch
    {
    auto gameModule() -> ::aegis::GameModule;
    }
}
#endif

namespace aegis::test
{
namespace
{
// The simulated time every run covers, a multiple of each frame time.
constexpr double gameplaySeconds{ 6.0 };
// The frame times of the runs, as the denominators of a second.
constexpr std::array gameplayRates{ 30, 60, 144 };
// How far the boxes' rest positions may lie apart across the runs, in meters: the runs apply the input in the same
// frames, so this only allows for rounding.
constexpr float restTolerance{ 0.01f };
// How far each box must have moved from where it began: the character pushed it.
constexpr float pushedDistance{ 0.25f };
// How far the marker's global matrix may differ from the platform's times its local one, per element.
constexpr float matrixTolerance{ 1e-4f };

// The scene's entities the checks read, by name.
constexpr std::string_view platformName{ "Platform" };
constexpr std::string_view markerName{ "Platform Marker" }; // The platform's child
constexpr std::string_view crateName{ "Spawned Crate" };    // The entity of the prefab the Spawn action spawns
constexpr std::string_view boxPrefix{ "Box " };

// An input event at a time of the simulation, in seconds.
struct TimedInput
{
    double seconds;
    platform::InputEvent event;
};

auto key(platform::Key key, platform::KeyAction action) -> platform::InputEvent
{
    return platform::KeyEvent{ .key = key, .action = action };
}

// The input of every run: walk forward (W, the axis MoveY) into the boxes for 3 s and press Spawn (E) at 1 s. A time is
// of the simulation: an event is applied before the first frame whose start has the world's fixed steps at or past it.
// Not the frame clock's: a run whose frame time doesn't add up to the step exactly in floats runs a step a frame late,
// which would let that step see the event early. Each time is a multiple of 1/6 s, where a frame of every frame time
// begins, so each run applies it before the same step.
const std::array timeline{
    TimedInput{ 0.0, key(platform::Key::W, platform::KeyAction::Press) },
    TimedInput{ 1.0, key(platform::Key::E, platform::KeyAction::Press) },
    TimedInput{ 1.5, key(platform::Key::E, platform::KeyAction::Release) },
    TimedInput{ 3.0, key(platform::Key::W, platform::KeyAction::Release) },
};
constexpr double spawnSeconds{ 1.0 };
// An event is due once the steps reach its time within this: a sum of steps is not exact.
constexpr double timelineEpsilon{ 1e-4 };

// What one run found, read once its engine is gone.
struct GameplayRun
{
    int rate{ 0 };
    std::map<std::string, math::Float3, std::less<>> start; // Each box's location before the first update
    std::map<std::string, math::Float3, std::less<>> rest;  // ... and after the last
    std::uint32_t crates{ 0 };                              // Entities named crateName after the last update
    std::uint32_t platformChecked{ 0 };                     // Frames in which the platform had moved and the marker was checked
    std::uint64_t soundsStarted{ 0 };
    std::vector<std::string> errors;
};

auto findByName(world::Registry& registry, std::string_view name) -> world::Entity
{
    for (auto&& [entity, entityName] : registry.view<Name>().each())
    {
        if (entityName.name == name)
            return world::Entity{ entity };
    }
    return {};
}

auto countByName(world::Registry& registry, std::string_view name) -> std::uint32_t
{
    std::uint32_t count{ 0 };
    for (auto&& [entity, entityName] : registry.view<Name>().each())
    {
        if (entityName.name == name)
            ++count;
    }
    return count;
}

// The boxes: the dynamic bodies named boxPrefix and a number, by name, at their Transform's location.
auto boxLocations(world::Registry& registry) -> std::map<std::string, math::Float3, std::less<>>
{
    std::map<std::string, math::Float3, std::less<>> boxes;
    for (auto&& [entity, body, name, transform] : registry.view<physics::RigidBody, Name, Transform>().each())
    {
        if (body.motionType == physics::MotionType::Dynamic && name.name.starts_with(boxPrefix))
            boxes.insert_or_assign(name.name, transform.location);
    }
    return boxes;
}

auto maxDifference(const math::Float4x4& a, const math::Float4x4& b) -> float
{
    float difference{ 0.0f };
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
            difference = std::max(difference, std::abs(a[row, column] - b[row, column]));
    }
    return difference;
}

// Plays the timeline into the engine's headless input and watches the scene while it runs (see runGameplay).
class GameplayShell final : public Shell
{
public:
    GameplayShell(GameplayRun& run, std::uint32_t frames) :
        m_run{ run },
        m_frames{ frames }
    {
    }

    auto onStartup(Engine& engine) -> std::expected<void, EngineError> override
    {
        m_input = engine.headlessInput();
        if (!m_input)
            return std::unexpected{ EngineError{ .code = EngineErrorCode::InvalidArguments, .message = "--gameplay needs the headless input" } };
        return {};
    }

    void onFrame(FrameContext& frame) override
    {
        auto& registry = frame.world.registry();
        // The simulation's time as of the last update: its fixed steps so far
        const auto& time = registry.time();
        const double now = static_cast<double>(time.steps) * time.fixedStepSeconds;
        if (m_frame == 0)
        {
            m_run.start = boxLocations(registry);
            m_platform = findByName(registry, platformName);
            m_marker = findByName(registry, markerName);
            if (!m_platform || !m_marker)
                error(std::format("the scene has no '{}' or no '{}'", platformName, markerName));
        }

        m_input->beginFrame();
        for (; m_next < timeline.size() && timeline[m_next].seconds <= now + timelineEpsilon; ++m_next)
            m_input->apply(timeline[m_next].event);

        if (now + timelineEpsilon < spawnSeconds && countByName(registry, crateName) != 0)
            error(std::format("'{}' exists at {:.3f} s, before the Spawn action", crateName, now));
        checkPlatform(registry);
        frame.update(frame.world, world::UpdateMode::Play);
        ++m_frame;
    }

    // After the last update: where the boxes came to rest, and the crate.
    void onShutdown() override
    {
        if (m_frame != m_frames || !m_world)
            return;
        auto& registry = m_world->registry();
        m_run.rest = boxLocations(registry);
        m_run.crates = countByName(registry, crateName);
    }

    void setWorld(world::World& world) { m_world = &world; }

private:
    // As the last update left them: in a frame after the platform moved, its child's global matrix is the platform's
    // times the child's local one, so the child followed in the same update (#257).
    void checkPlatform(world::Registry& registry)
    {
        if (!m_platform || !m_marker || !registry.has<GlobalTransform>(m_platform) || !registry.has<GlobalTransform>(m_marker))
            return;
        const auto platform = registry.get<GlobalTransform>(m_platform).matrix;
        const bool moved = m_lastPlatform && maxDifference(platform, *m_lastPlatform) > 0.0f;
        m_lastPlatform = platform;
        if (!moved)
            return;
        const auto expected = platform * registry.get<Transform>(m_marker).matrix();
        const auto difference = maxDifference(registry.get<GlobalTransform>(m_marker).matrix, expected);
        if (difference > matrixTolerance)
            error(std::format("in frame {} the platform moved, and its child's global matrix differs from the platform's times its "
                              "local one by {}",
                m_frame, difference));
        ++m_run.platformChecked;
    }

    void error(std::string message)
    {
        // One per kind is enough: a broken check fails every frame
        if (std::ranges::find(m_run.errors, message) == m_run.errors.end() && m_run.errors.size() < 10)
            m_run.errors.push_back(std::move(message));
    }

    GameplayRun& m_run;
    std::uint32_t m_frames;
    platform::InputState* m_input{ nullptr };
    world::World* m_world{ nullptr };
    std::uint32_t m_frame{ 0 };
    std::size_t m_next{ 0 }; // The first event of the timeline not applied yet
    world::Entity m_platform;
    world::Entity m_marker;
    std::optional<math::Float4x4> m_lastPlatform;
};

// Runs the gameplay scene once, its world's clock fixed at 1/rate s, for gameplaySeconds.
auto runOnce([[maybe_unused]] const Options& options, int rate, [[maybe_unused]] bool last) -> GameplayRun
{
    GameplayRun run{ .rate = rate };
#ifdef AEGIS_TEST_EXAMPLES
    const auto frameSeconds = 1.0f / static_cast<float>(rate);
    const auto frames = static_cast<std::uint32_t>(std::lround(gameplaySeconds * rate));
    auto engine = Engine::create({
        .title = "aegis-test",
        .width = gameplayExtent.x,
        .height = gameplayExtent.y,
        .headless = true,
        .frames = frames,
        .screenshot = last ? options.screenshot : std::string{},
    });
    if (!engine)
    {
        run.errors.push_back(std::format("failed to create the engine: {}", describe(engine.error())));
        return run;
    }

    // The game's types before its scene loads, and its systems in every world, as the launcher does
    const auto game = aegis::launch::gameModule();
    if (game.registerTypes)
    {
        if (auto registered = game.registerTypes((*engine)->types()); !registered)
        {
            run.errors.push_back(std::format("registering the {} game's types failed: {}", game.name, reflect::describe(registered.error())));
            return run;
        }
    }
    if (game.setupWorld)
        (*engine)->setWorldSetup([&engine = **engine, setup = game.setupWorld](world::World& world) { setup(engine, world); });

    auto shell = std::make_unique<GameplayShell>(run, frames);
    shell->setWorld((*engine)->world());
    (*engine)->setShell(std::move(shell));
    (*engine)->world().clock() = core::FrameClock::fixed(frameSeconds);

    const auto scene = (*engine)->assets().find(gameplaySceneFile);
    if (!scene)
    {
        run.errors.push_back(std::format("'{}' is not in the sample project", gameplaySceneFile));
        return run;
    }
    if (auto loaded = (*engine)->loadScene(*scene); !loaded)
    {
        run.errors.push_back(describe(loaded.error()));
        return run;
    }
    if ((*engine)->run() != exitSuccess)
        run.errors.push_back("the engine's run failed (see the log)");
    run.soundsStarted = (*engine)->audio().stats().started;
    engine->reset(); // The shell has finished with the run
#else
    run.errors.push_back("aegis-test was built without the Examples game (the samples are not checked out)");
#endif
    return run;
}

// The checks of one run on its own.
auto checkRun(const GameplayRun& run) -> std::vector<std::string>
{
    auto errors = run.errors;
    if (run.rest.size() != 5 || run.start.size() != 5)
        errors.push_back(std::format("{} boxes at the start and {} at the end, expected 5", run.start.size(), run.rest.size()));
    for (const auto& [name, rest] : run.rest)
    {
        const auto start = run.start.find(name);
        if (start != run.start.end() && math::length(rest - start->second) < pushedDistance)
            errors.push_back(std::format("'{}' moved {:.3f} m, less than {} m: the character didn't push it", name, math::length(rest - start->second), pushedDistance));
    }
    if (run.crates != 1)
        errors.push_back(std::format("{} '{}' entities after the Spawn action, expected 1", run.crates, crateName));
    if (run.soundsStarted != 1)
        errors.push_back(std::format("{} sounds started, expected the one the character's first push plays", run.soundsStarted));
    if (run.platformChecked == 0)
        errors.push_back("the platform never moved, so its child was never checked");
    return errors;
}
}

auto runGameplay(const Options& options) -> int
{
    int exitCode{ exitSuccess };
    std::vector<GameplayRun> runs;
    for (const auto rate : gameplayRates)
    {
        runs.push_back(runOnce(options, rate, rate == gameplayRates.back()));
        const auto& run = runs.back();
        const auto errors = checkRun(run);
        for (const auto& message : errors)
            std::println("Gameplay check failed at 1/{} s: {}", rate, message);
        if (!errors.empty())
            exitCode = exitFailure;
        else
            std::println("Gameplay at 1/{} s: the crate spawned, one sound started, the platform's child followed it in {} frames", rate, run.platformChecked);
    }

    // The same simulation at every frame time: each box comes to rest at the same place.
    float worst{ 0.0f };
    for (const auto& [name, rest] : runs.front().rest)
    {
        for (const auto& other : runs | std::views::drop(1))
        {
            const auto found = other.rest.find(name);
            if (found == other.rest.end())
                continue; // checkRun reported the missing box
            const auto distance = math::length(found->second - rest);
            worst = std::max(worst, distance);
            if (distance > restTolerance)
            {
                std::println("Gameplay check failed: '{}' rests {:.4f} m apart at 1/{} s and 1/{} s, more than {} m", name, distance,
                    runs.front().rate, other.rate, restTolerance);
                exitCode = exitFailure;
            }
        }
    }
    if (exitCode == exitSuccess)
        std::println("Gameplay verified: the boxes rest at most {:.5f} m apart at 1/30, 1/60 and 1/144 s", worst);
    return exitCode;
}
}
