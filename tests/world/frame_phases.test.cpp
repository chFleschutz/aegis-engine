#include <doctest/doctest.h>

import std;
import aegis.core;
import aegis.math;
import aegis.world;

namespace
{
using aegis::world::FixedStep;
using aegis::world::Phase;
using aegis::world::Registry;
using aegis::world::System;
using aegis::world::UpdateMode;
using aegis::world::World;

// Logs its hooks; its fixed steps only if Fixed, with the world's step count
template<Phase P, bool Edit = false, bool Fixed = false>
class Recorder : public System
{
public:
    Recorder(std::vector<std::string>& log, std::string name) :
        m_log(log),
        m_name(std::move(name))
    {
    }

    [[nodiscard]] auto phase() const -> Phase override { return P; }
    [[nodiscard]] auto runsInEdit() const -> bool override { return Edit; }
    void onBegin(Registry&) override { m_log.push_back(m_name + ".begin"); }
    void onUpdate(Registry&, float) override { m_log.push_back(m_name); }
    void onFixedUpdate(Registry& registry, float step) override
    {
        CHECK(step == registry.time().fixedStepSeconds);
        if constexpr (Fixed)
            m_log.push_back(m_name + ".fixed" + std::to_string(registry.time().steps));
    }

private:
    std::vector<std::string>& m_log;
    std::string m_name;
};
}

TEST_SUITE("world::World phases")
{
    TEST_CASE("systems run phase by phase, not in the order they were added")
    {
        std::vector<std::string> log;
        World scene;
        scene.addSystem<Recorder<Phase::LateUpdate>>(log, "late");
        scene.addSystem<Recorder<Phase::PostUpdate>>(log, "post");
        scene.addSystem<Recorder<Phase::Update>>(log, "update");
        scene.addSystem<Recorder<Phase::PreUpdate>>(log, "pre");
        scene.update(0.016f);

        CHECK(log == std::vector<std::string>{ "pre", "update", "post", "late" });
    }

    TEST_CASE("the fixed steps run between the pre-update and the update phase")
    {
        std::vector<std::string> log;
        World scene;
        scene.addSystem<Recorder<Phase::Update, false, true>>(log, "update");
        scene.addSystem<Recorder<Phase::FixedUpdate, false, true>>(log, "fixed");
        scene.addSystem<Recorder<Phase::PreUpdate, false, true>>(log, "pre");
        scene.fixedStep().setStep(0.25f);
        scene.update(0.5f);

        // Every system's onFixedUpdate once per step, in the order they were added; then the onUpdate of the
        // systems in the fixed-update phase
        CHECK(log == std::vector<std::string>{ "pre", "update.fixed1", "fixed.fixed1", "pre.fixed1", "update.fixed2", "fixed.fixed2", "pre.fixed2", "fixed", "update" });
    }

    TEST_CASE("an update runs as many fixed steps as the frame completes")
    {
        std::vector<std::string> log;
        World scene;
        scene.addSystem<Recorder<Phase::Update, false, true>>(log, "s");
        scene.fixedStep().setStep(0.25f);

        scene.update(0.125f); // Shorter than a step
        CHECK(log == std::vector<std::string>{ "s" });
        scene.update(0.125f); // Completes the first
        CHECK(log == std::vector<std::string>{ "s", "s.fixed1", "s" });
        log.clear();
        scene.update(0.75f); // Three
        CHECK(log == std::vector<std::string>{ "s.fixed2", "s.fixed3", "s.fixed4", "s" });
    }

    TEST_CASE("a frame clock fixed to the step runs exactly one fixed step per update")
    {
        World scene;
        scene.clock() = aegis::core::FrameClock::fixed(FixedStep::DEFAULT_STEP_SEC); // What a headless engine sets
        int steps = 0;
        class Counter : public System
        {
        public:
            explicit Counter(int& steps) :
                m_steps(steps)
            {
            }
            void onFixedUpdate(Registry&, float) override { ++m_steps; }

        private:
            int& m_steps;
        };
        scene.addSystem<Counter>(steps);
        for (int frame = 1; frame <= 600; ++frame)
        {
            scene.update(scene.clock().tick(aegis::core::FrameClock::Clock::now()));
            REQUIRE(steps == frame);
        }
        CHECK(scene.time().steps == 600);
        CHECK(scene.time().alpha == 0.0f);
    }

    TEST_CASE("the world's time follows its updates and is the registry's")
    {
        World scene;
        scene.fixedStep().setStep(0.25f);
        CHECK(scene.time().frame == 0);
        CHECK(scene.time().steps == 0);

        scene.update(0.375f);
        scene.update(0.25f);
        const auto& time = scene.registry().time();
        CHECK(&time == &scene.time());
        CHECK(time.deltaSeconds == 0.25f);
        CHECK(time.fixedStepSeconds == 0.25f);
        CHECK(time.elapsedSeconds == 0.625);
        CHECK(time.frame == 2);
        CHECK(time.steps == 2);
        CHECK(time.alpha == 0.5f);

        scene.reset(); // The time belongs to the world, not to the scene in it
        CHECK(scene.time().frame == 2);
        CHECK(scene.fixedStep().step() == 0.25f);
    }

    TEST_CASE("a registry without a world has the default time")
    {
        const Registry registry;
        CHECK(registry.time().frame == 0);
        CHECK(registry.time().fixedStepSeconds == FixedStep::DEFAULT_STEP_SEC);
    }

    TEST_CASE("systems of one phase run in the order they were added")
    {
        std::vector<std::string> log;
        World scene;
        scene.addSystem<Recorder<Phase::Update>>(log, "a");
        scene.addSystem<Recorder<Phase::Update>>(log, "b");
        scene.addSystem<Recorder<Phase::Update>>(log, "c");
        scene.update(0.016f);

        CHECK(log == std::vector<std::string>{ "a", "b", "c" });
    }

    TEST_CASE("a system is in the update phase by default")
    {
        CHECK(System{}.phase() == Phase::Update);
    }

    TEST_CASE("the camera view follows the transform written this frame whatever the registration order")
    {
        World scene;
        scene.addSystem<aegis::world::CameraSystem>(); // Added before the transforms on purpose
        scene.addSystem<aegis::world::TransformSystem>();
        auto& registry = scene.registry();
        const auto camera = registry.create("Camera");
        registry.add<aegis::Camera>(camera);
        registry.add<aegis::DynamicTag>(camera); // Static entities keep their transform after begin
        scene.begin();

        registry.get<aegis::Transform>(camera).location = { 3.0f, 0.0f, 0.0f };
        scene.update(0.016f);

        const float translationX = registry.get<aegis::CameraMatrices>(camera).view[0, 3];
        CHECK(translationX != 0.0f); // The view already saw the move
        scene.update(0.016f);        // Nothing moved: the view must not change
        CHECK(registry.get<aegis::CameraMatrices>(camera).view[0, 3] == doctest::Approx(translationX));
        CHECK(registry.get<aegis::GlobalTransform>(camera).location().x == doctest::Approx(3.0f));
    }

    TEST_CASE("deferred commands run in order at the start of the next update, before any system")
    {
        std::vector<std::string> log;
        World scene;
        scene.addSystem<Recorder<Phase::PreUpdate>>(log, "system");
        scene.defer([&log](Registry&) { log.push_back("first"); });
        scene.defer([&log](Registry&) { log.push_back("second"); });
        CHECK(log.empty());

        scene.update(0.016f);
        CHECK(log == std::vector<std::string>{ "first", "second", "system" });

        scene.update(0.016f); // Each command runs once
        CHECK(log.size() == 4);
    }

    TEST_CASE("a deferred command can create and destroy entities")
    {
        World scene;
        auto& registry = scene.registry();
        const auto doomed = registry.create("Doomed");
        aegis::world::Entity created{};
        scene.defer([&created](Registry& r) { created = r.create("Created"); });
        scene.defer([doomed](Registry& r) { r.destroy(doomed); });
        scene.update(0.016f);

        CHECK(registry.valid(created));
        CHECK_FALSE(registry.valid(doomed));
    }

    TEST_CASE("a command deferred by a command waits for the next update")
    {
        int runs = 0;
        World scene;
        scene.defer([&](Registry&) {
            ++runs;
            scene.defer([&](Registry&) { ++runs; });
        });
        scene.update(0.016f);
        CHECK(runs == 1);
        scene.update(0.016f);
        CHECK(runs == 2);
    }
}

TEST_SUITE("world::World update modes")
{
    TEST_CASE("play mode runs every system")
    {
        std::vector<std::string> log;
        World scene;
        scene.addSystem<Recorder<Phase::Update>>(log, "game");
        scene.addSystem<Recorder<Phase::PostUpdate, true>>(log, "derived");
        scene.begin(UpdateMode::Play);
        scene.update(0.016f, UpdateMode::Play);

        CHECK(log == std::vector<std::string>{ "game.begin", "derived.begin", "game", "derived" });
    }

    TEST_CASE("edit mode begins and updates only the systems that opt in")
    {
        std::vector<std::string> log;
        World scene;
        scene.addSystem<Recorder<Phase::Update>>(log, "game");
        scene.addSystem<Recorder<Phase::PostUpdate, true>>(log, "derived");
        scene.begin(UpdateMode::Edit);
        scene.update(0.016f, UpdateMode::Edit);

        CHECK(log == std::vector<std::string>{ "derived.begin", "derived" });
    }

    TEST_CASE("a system stays out of edit mode by default")
    {
        CHECK_FALSE(System{}.runsInEdit());
        CHECK(aegis::world::TransformSystem{}.runsInEdit());
        CHECK(aegis::world::CameraSystem{}.runsInEdit());
    }

    TEST_CASE("edit mode runs no fixed steps, while the frame time advances")
    {
        std::vector<std::string> log;
        World scene;
        scene.addSystem<Recorder<Phase::PostUpdate, true, true>>(log, "derived");
        scene.fixedStep().setStep(0.25f);
        scene.update(1.0f, UpdateMode::Edit);

        CHECK(log == std::vector<std::string>{ "derived" });
        CHECK(scene.time().frame == 1);
        CHECK(scene.time().elapsedSeconds == 1.0);
        CHECK(scene.time().steps == 0);
        CHECK(scene.fixedStep().remainder() == 0.0); // Nothing gathered towards a step either
    }

    TEST_CASE("edit mode applies the deferred commands")
    {
        int runs = 0;
        World scene;
        scene.defer([&runs](Registry&) { ++runs; });
        scene.update(0.016f, UpdateMode::Edit);
        CHECK(runs == 1);
    }

    TEST_CASE("an edited transform reaches the camera in edit mode")
    {
        World scene;
        scene.addSystem<aegis::world::TransformSystem>();
        scene.addSystem<aegis::world::CameraSystem>();
        auto& registry = scene.registry();
        const auto camera = registry.create("Camera");
        registry.add<aegis::Camera>(camera);
        scene.begin(UpdateMode::Edit);

        registry.get<aegis::Transform>(camera).location = { 3.0f, 0.0f, 0.0f };
        registry.markStaticDirty(camera); // What the editor does after moving a static entity
        scene.update(0.016f, UpdateMode::Edit);

        CHECK(registry.get<aegis::GlobalTransform>(camera).location().x == doctest::Approx(3.0f));
        CHECK(registry.get<aegis::CameraMatrices>(camera).view[0, 3] != 0.0f);
    }
}
