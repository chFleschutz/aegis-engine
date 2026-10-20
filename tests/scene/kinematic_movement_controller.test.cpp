#include <doctest/doctest.h>

import std;
import aegis.math;
import aegis.platform;
import aegis.reflect;
import aegis.scene;
import aegis.scripting.script;
import aegis.scripting.script_system;
import aegis.scripting.movement.kinematic_movement_controller;

namespace
{
using namespace aegis;
using namespace aegis::scripting;

// A camera entity driven by a hand-built InputState, one update() per frame.
struct FlyCamera
{
    FlyCamera()
    {
        REQUIRE(types.component<Transform>("Transform"));
        REQUIRE(registerKinematicMovementController(types));
        scene.addSystem<ScriptSystem>(scene.registry(), input);
        entity = scene.registry().create("Camera");
        REQUIRE(attachScript(scene.registry(), types, entity, "KinematicMovementController"));
        scene.begin();
    }

    // One frame of 'deltaSeconds' in which the given events happen
    void frame(float deltaSeconds, std::initializer_list<platform::InputEvent> events = {})
    {
        input.beginFrame();
        for (const auto& event : events)
            input.apply(event);
        scene.update(deltaSeconds);
    }

    auto transform() -> Transform& { return scene.registry().get<Transform>(entity); }

    static auto rightButton(platform::KeyAction action) -> platform::InputEvent
    {
        return platform::MouseButtonEvent{ platform::MouseButton::Right, action, platform::Modifiers::None };
    }

    static auto key(platform::Key key, platform::KeyAction action) -> platform::InputEvent
    {
        return platform::KeyEvent{ key, action, platform::Modifiers::None };
    }

    platform::InputState input;
    reflect::TypeRegistry types;
    scene::Scene scene;
    scene::Entity entity;
};
}

TEST_SUITE("scripting::KinematicMovementController")
{
    TEST_CASE("turns by the same angle for the same mouse movement at any frame rate")
    {
        constexpr auto pressed = platform::KeyAction::Press;

        FlyCamera slow;
        slow.input.placeCursor({ 100.0, 100.0 });
        slow.frame(1.0f / 30.0f, { FlyCamera::rightButton(pressed) }); // Starts the look
        slow.frame(1.0f / 30.0f, { platform::CursorEvent{ 140.0, 120.0 } });

        FlyCamera fast;
        fast.input.placeCursor({ 100.0, 100.0 });
        fast.frame(1.0f / 120.0f, { FlyCamera::rightButton(pressed) });
        fast.frame(1.0f / 120.0f, { platform::CursorEvent{ 120.0, 110.0 } });
        fast.frame(1.0f / 120.0f, { platform::CursorEvent{ 140.0, 120.0 } });

        CHECK(slow.transform().rotation.x == doctest::Approx(fast.transform().rotation.x));
        CHECK(slow.transform().rotation.y == doctest::Approx(fast.transform().rotation.y));
        CHECK(slow.transform().rotation.z == doctest::Approx(fast.transform().rotation.z));
        CHECK(slow.transform().rotation.w == doctest::Approx(fast.transform().rotation.w));
        CHECK(slow.transform().rotation.w < 0.999f); // It turned
    }

    TEST_CASE("does not turn without the look button held")
    {
        FlyCamera camera;
        camera.input.placeCursor({ 100.0, 100.0 });
        camera.frame(1.0f / 60.0f, { platform::CursorEvent{ 140.0, 120.0 } });

        CHECK(camera.transform().rotation.w == 1.0f);
    }

    TEST_CASE("moves forward at the configured speed while W is down")
    {
        FlyCamera camera;
        const auto forward = camera.transform().forward();
        camera.frame(0.5f, { FlyCamera::key(platform::Key::W, platform::KeyAction::Press) });

        const auto moved = camera.transform().location;
        CHECK(glm::length(moved) == doctest::Approx(KinematicMovementController::DEFAULT_MOVE_SPEED * 0.5f));
        CHECK(glm::dot(glm::normalize(moved), forward) == doctest::Approx(1.0f));
    }

    TEST_CASE("does not move while the keyboard is captured")
    {
        FlyCamera camera;
        camera.input.setCaptured(true, false);
        camera.frame(0.5f, { FlyCamera::key(platform::Key::W, platform::KeyAction::Press) });

        CHECK(glm::length(camera.transform().location) == 0.0f);
    }
}
