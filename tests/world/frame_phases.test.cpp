#include <doctest/doctest.h>

import std;
import aegis.math;
import aegis.world;
import aegis.world.components;
import aegis.world.system;
import aegis.world.systems.camera_system;
import aegis.world.systems.transform_system;

namespace
{
using aegis::world::Phase;
using aegis::world::Registry;
using aegis::world::World;
using aegis::world::System;

template<Phase P>
class Recorder : public System
{
public:
    Recorder(std::vector<std::string>& log, std::string name) :
        m_log(log),
        m_name(std::move(name))
    {
    }

    [[nodiscard]] auto phase() const -> Phase override { return P; }
    void onUpdate(Registry&, float) override { m_log.push_back(m_name); }

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

        const float translationX = registry.get<aegis::Camera>(camera).viewMatrix[3][0];
        CHECK(translationX != 0.0f); // The view already saw the move
        scene.update(0.016f);        // Nothing moved: the view must not change
        CHECK(registry.get<aegis::Camera>(camera).viewMatrix[3][0] == doctest::Approx(translationX));
        CHECK(registry.get<aegis::GlobalTransform>(camera).location.x == doctest::Approx(3.0f));
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
