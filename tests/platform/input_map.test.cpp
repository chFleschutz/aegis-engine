#include <doctest/doctest.h>

import std;
import aegis.core;
import aegis.platform;

namespace
{
using namespace aegis;
using namespace aegis::platform;

auto press(Key key) -> InputEvent
{
    return KeyEvent{ key, KeyAction::Press, Modifiers::None };
}
auto release(Key key) -> InputEvent
{
    return KeyEvent{ key, KeyAction::Release, Modifiers::None };
}
auto click(MouseButton button) -> InputEvent
{
    return MouseButtonEvent{ button, KeyAction::Press, Modifiers::None };
}

auto pad(std::initializer_list<GamepadButton> buttons, std::initializer_list<std::pair<GamepadAxis, float>> axes = {}) -> InputEvent
{
    GamepadEvent event{ .connected = true };
    for (const auto button : buttons)
        event.buttons[static_cast<std::size_t>(button)] = true;
    for (const auto& [axis, value] : axes)
        event.axes[static_cast<std::size_t>(axis)] = value;
    return event;
}

// A state with "Jump" (Space, GamepadA), "Fire" (MouseLeft, GamepadRightTrigger) and "MoveX" (A/D, GamepadLeftX).
auto makeState() -> InputState
{
    InputMap map;
    REQUIRE(map.addAction("Jump", { Key::Space, GamepadButton::A }));
    REQUIRE(map.addAction("Fire", { MouseButton::Left, GamepadAxis::RightTrigger }));
    REQUIRE(map.addAxis("MoveX", { AxisPair{ Key::A, Key::D }, GamepadAxis::LeftX }));
    InputState state;
    state.setMap(std::move(map));
    return state;
}

class LogCapture
{
public:
    LogCapture()
    {
        log::setSink([this](log::Level, std::string_view message) { lines.emplace_back(message); });
    }
    LogCapture(const LogCapture&) = delete;
    auto operator=(const LogCapture&) -> LogCapture& = delete;
    ~LogCapture() { log::setSink({}); }

    std::vector<std::string> lines;
};
}

TEST_SUITE("platform::InputMap")
{
    TEST_CASE("parseInputSource reads keys, mouse buttons and gamepad inputs by name")
    {
        CHECK(parseInputSource("Space") == InputSource{ Key::Space });
        CHECK(parseInputSource("LeftShift") == InputSource{ Key::LeftShift });
        CHECK(parseInputSource("F12") == InputSource{ Key::F12 });
        CHECK(parseInputSource("MouseLeft") == InputSource{ MouseButton::Left });
        CHECK(parseInputSource("MouseButton4") == InputSource{ MouseButton::Button4 });
        CHECK(parseInputSource("GamepadA") == InputSource{ GamepadButton::A });
        CHECK(parseInputSource("GamepadDpadUp") == InputSource{ GamepadButton::DpadUp });
        CHECK(parseInputSource("GamepadRightTrigger") == InputSource{ GamepadAxis::RightTrigger });
    }

    TEST_CASE("parseInputSource rejects unknown names, Unknown and other case")
    {
        CHECK_FALSE(parseInputSource("Unknown").has_value());
        CHECK_FALSE(parseInputSource("space").has_value());
        CHECK_FALSE(parseInputSource("Mouse").has_value());
        CHECK_FALSE(parseInputSource("").has_value());
    }

    TEST_CASE("inputSourceName is the reverse of parseInputSource")
    {
        for (const auto name : { "Space", "KeyPadEnter", "MouseMiddle", "GamepadStart", "GamepadLeftY" })
        {
            const auto source = parseInputSource(name);
            REQUIRE(source.has_value());
            CHECK(inputSourceName(*source) == name);
        }
        CHECK(inputSourceName(Key::Unknown).empty());
    }

    TEST_CASE("a name is taken once across actions and axes")
    {
        InputMap map;
        CHECK(map.empty());
        CHECK(map.addAction("Jump", { Key::Space }));
        CHECK_FALSE(map.addAction("Jump", { Key::Enter }));
        CHECK_FALSE(map.addAxis("Jump", { GamepadAxis::LeftX }));
        CHECK(map.action("Jump")->front() == InputSource{ Key::Space });
        CHECK(map.axis("Jump") == nullptr);
        CHECK(map.action("Fire") == nullptr);
        CHECK_FALSE(map.empty());
    }
}

TEST_SUITE("platform::InputState actions")
{
    TEST_CASE("an action is down and pressed with any of its inputs")
    {
        auto state = makeState();
        state.beginFrame();
        state.apply(press(Key::Space));
        CHECK(state.action("Jump").down());
        CHECK(state.action("Jump").pressed());
        CHECK_FALSE(state.action("Jump").released());

        state.beginFrame();
        CHECK(state.action("Jump").down());
        CHECK_FALSE(state.action("Jump").pressed());

        state.beginFrame();
        state.apply(release(Key::Space));
        CHECK_FALSE(state.action("Jump").down());
        CHECK(state.action("Jump").released());
    }

    TEST_CASE("a second input of a held action is no new press and no release")
    {
        auto state = makeState();
        state.beginFrame();
        state.apply(press(Key::Space));
        state.beginFrame();
        state.apply(pad({ GamepadButton::A }));
        CHECK(state.action("Jump").down());
        CHECK_FALSE(state.action("Jump").pressed());

        state.beginFrame();
        state.apply(release(Key::Space));
        state.apply(pad({ GamepadButton::A }));
        CHECK(state.action("Jump").down());
        CHECK_FALSE(state.action("Jump").released());

        state.beginFrame();
        state.apply(pad({}));
        CHECK_FALSE(state.action("Jump").down());
        CHECK(state.action("Jump").released());
    }

    TEST_CASE("a key tapped within one frame presses and releases the action")
    {
        auto state = makeState();
        state.beginFrame();
        state.apply(press(Key::Space));
        state.apply(release(Key::Space));
        CHECK_FALSE(state.action("Jump").down());
        CHECK(state.action("Jump").pressed());
        CHECK(state.action("Jump").released());
    }

    TEST_CASE("a gamepad button gives the action edges from frame to frame")
    {
        auto state = makeState();
        state.beginFrame();
        state.apply(pad({ GamepadButton::A }));
        CHECK(state.action("Jump").pressed());
        state.beginFrame();
        state.apply(pad({ GamepadButton::A }));
        CHECK(state.action("Jump").down());
        CHECK_FALSE(state.action("Jump").pressed());
    }

    TEST_CASE("a trigger bound to an action is down past half way")
    {
        auto state = makeState();
        state.beginFrame();
        state.apply(pad({}, { { GamepadAxis::RightTrigger, 0.4f } }));
        CHECK_FALSE(state.action("Fire").down());
        state.beginFrame();
        state.apply(pad({}, { { GamepadAxis::RightTrigger, 0.6f } }));
        CHECK(state.action("Fire").down());
        CHECK(state.action("Fire").pressed());
    }

    TEST_CASE("a disconnected gamepad has nothing down")
    {
        auto state = makeState();
        state.beginFrame();
        state.apply(pad({ GamepadButton::A }));
        state.beginFrame();
        auto event = std::get<GamepadEvent>(pad({ GamepadButton::A }));
        event.connected = false;
        state.apply(event);
        CHECK_FALSE(state.gamepadConnected());
        CHECK_FALSE(state.action("Jump").down());
        CHECK(state.action("Jump").released());
    }

    TEST_CASE("actions read nothing from a captured device, the gamepad with the keyboard")
    {
        auto state = makeState();
        state.beginFrame();
        state.apply(press(Key::Space));
        state.apply(click(MouseButton::Left));

        state.setCaptured(true, false);
        CHECK_FALSE(state.action("Jump").down());
        CHECK_FALSE(state.action("Jump").pressed());
        CHECK(state.action("Fire").down());

        state.apply(pad({ GamepadButton::A }));
        CHECK_FALSE(state.action("Jump").down());

        state.setCaptured(false, true);
        CHECK(state.action("Jump").down());
        CHECK_FALSE(state.action("Fire").down());
    }

    TEST_CASE("releasing the capture over a held key is no new press")
    {
        auto state = makeState();
        state.beginFrame();
        state.apply(press(Key::Space));
        state.setCaptured(true, false);
        state.beginFrame();
        state.setCaptured(false, false);
        CHECK(state.action("Jump").down());
        CHECK_FALSE(state.action("Jump").pressed());
    }

    TEST_CASE("an unknown action is never down and warns once")
    {
        LogCapture capture;
        auto state = makeState();
        state.beginFrame();
        state.apply(press(Key::Space));
        CHECK_FALSE(state.action("Jmup").down());
        CHECK_FALSE(state.action("Jmup").pressed());
        CHECK(state.axis("Jmup") == 0.0f);
        REQUIRE(capture.lines.size() == 2);
        CHECK(capture.lines[0].contains("action named 'Jmup'"));
        CHECK(capture.lines[1].contains("axis named 'Jmup'"));
    }
}

TEST_SUITE("platform::InputState axes")
{
    TEST_CASE("a key pair gives -1, 0 or 1")
    {
        auto state = makeState();
        state.beginFrame();
        CHECK(state.axis("MoveX") == 0.0f);
        state.apply(press(Key::D));
        CHECK(state.axis("MoveX") == 1.0f);
        state.apply(press(Key::A));
        CHECK(state.axis("MoveX") == 0.0f);
        state.apply(release(Key::D));
        CHECK(state.axis("MoveX") == -1.0f);
    }

    TEST_CASE("a gamepad axis reads 0 inside the dead zone and stretches the rest")
    {
        auto state = makeState();
        state.beginFrame();
        state.apply(pad({}, { { GamepadAxis::LeftX, 0.1f } }));
        CHECK(state.axis("MoveX") == 0.0f);
        state.apply(pad({}, { { GamepadAxis::LeftX, -1.0f } }));
        CHECK(state.axis("MoveX") == doctest::Approx(-1.0f));
        state.apply(pad({}, { { GamepadAxis::LeftX, 0.575f } }));
        CHECK(state.axis("MoveX") == doctest::Approx(0.5f));
        CHECK(state.gamepadAxis(GamepadAxis::LeftX) == doctest::Approx(0.575f));
    }

    TEST_CASE("the binding with the largest magnitude wins")
    {
        auto state = makeState();
        state.beginFrame();
        state.apply(pad({}, { { GamepadAxis::LeftX, 0.575f } }));
        state.apply(press(Key::A));
        CHECK(state.axis("MoveX") == -1.0f);
        state.apply(release(Key::A));
        CHECK(state.axis("MoveX") == doctest::Approx(0.5f));
    }

    TEST_CASE("axes read 0 while their device is captured")
    {
        auto state = makeState();
        state.beginFrame();
        state.apply(press(Key::D));
        state.apply(pad({}, { { GamepadAxis::LeftX, -1.0f } }));
        state.setCaptured(false, true);
        CHECK(state.axis("MoveX") == 1.0f);
        state.setCaptured(true, false);
        CHECK(state.axis("MoveX") == 0.0f);
    }
}
