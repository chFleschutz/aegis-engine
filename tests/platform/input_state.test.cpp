#include <doctest/doctest.h>

import std;
import aegis.platform;

namespace
{
using namespace aegis::platform;

auto down(Key key, Modifiers mods = Modifiers::None) -> InputEvent
{
    return KeyEvent{ key, KeyAction::Press, mods };
}
auto up(Key key) -> InputEvent
{
    return KeyEvent{ key, KeyAction::Release, Modifiers::None };
}
auto repeat(Key key) -> InputEvent
{
    return KeyEvent{ key, KeyAction::Repeat, Modifiers::None };
}
auto click(MouseButton button) -> InputEvent
{
    return MouseButtonEvent{ button, KeyAction::Press, Modifiers::None };
}
auto unclick(MouseButton button) -> InputEvent
{
    return MouseButtonEvent{ button, KeyAction::Release, Modifiers::None };
}
auto moveTo(double x, double y) -> InputEvent
{
    return CursorEvent{ x, y };
}
}

TEST_SUITE("platform::InputState")
{
    TEST_CASE("starts with nothing down")
    {
        InputState state;
        CHECK_FALSE(state.keyDown(Key::W));
        CHECK_FALSE(state.mouseDown(MouseButton::Left));
        CHECK(state.cursorDelta().x == 0.0);
        CHECK(state.scroll().y == 0.0);
        CHECK(state.modifiers() == Modifiers::None);
    }

    TEST_CASE("reports a key as down and pressed in the frame it went down")
    {
        InputState state;
        state.beginFrame();
        state.apply(down(Key::W));

        CHECK(state.keyDown(Key::W));
        CHECK(state.keyPressed(Key::W));
        CHECK_FALSE(state.keyReleased(Key::W));
        CHECK_FALSE(state.keyDown(Key::S));
    }

    TEST_CASE("keeps a held key down but no longer pressed in the next frame")
    {
        InputState state;
        state.beginFrame();
        state.apply(down(Key::W));
        state.beginFrame();

        CHECK(state.keyDown(Key::W));
        CHECK_FALSE(state.keyPressed(Key::W));
    }

    TEST_CASE("reports a key as released in the frame it went up")
    {
        InputState state;
        state.beginFrame();
        state.apply(down(Key::W));
        state.beginFrame();
        state.apply(up(Key::W));

        CHECK_FALSE(state.keyDown(Key::W));
        CHECK(state.keyReleased(Key::W));

        state.beginFrame();
        CHECK_FALSE(state.keyReleased(Key::W));
    }

    TEST_CASE("does not count a key repeat as a press")
    {
        InputState state;
        state.beginFrame();
        state.apply(down(Key::W));
        state.beginFrame();
        state.apply(repeat(Key::W));

        CHECK(state.keyDown(Key::W));
        CHECK_FALSE(state.keyPressed(Key::W));
    }

    TEST_CASE("sees a key pressed and released within one frame")
    {
        InputState state;
        state.beginFrame();
        state.apply(down(Key::W));
        state.apply(up(Key::W));

        CHECK_FALSE(state.keyDown(Key::W));
        CHECK(state.keyPressed(Key::W));
        CHECK(state.keyReleased(Key::W));
    }

    TEST_CASE("ignores Key::Unknown and out of range keys")
    {
        InputState state;
        state.beginFrame();
        state.apply(down(Key::Unknown));
        state.apply(down(static_cast<Key>(100000)));

        CHECK_FALSE(state.keyDown(Key::Unknown));
        CHECK_FALSE(state.keyPressed(Key::Unknown));
        CHECK_FALSE(state.keyDown(static_cast<Key>(100000)));
    }

    TEST_CASE("tracks mouse buttons like keys")
    {
        InputState state;
        state.beginFrame();
        state.apply(click(MouseButton::Right));
        CHECK(state.mouseDown(MouseButton::Right));
        CHECK(state.mousePressed(MouseButton::Right));
        CHECK_FALSE(state.mouseDown(MouseButton::Left));

        state.beginFrame();
        CHECK(state.mouseDown(MouseButton::Right));
        CHECK_FALSE(state.mousePressed(MouseButton::Right));

        state.apply(unclick(MouseButton::Right));
        CHECK_FALSE(state.mouseDown(MouseButton::Right));
        CHECK(state.mouseReleased(MouseButton::Right));
    }

    TEST_CASE("takes the first cursor position as a start and not a movement")
    {
        InputState state;
        state.beginFrame();
        state.apply(moveTo(100.0, 50.0));

        CHECK(state.cursor().x == 100.0);
        CHECK(state.cursor().y == 50.0);
        CHECK(state.cursorDelta().x == 0.0);
        CHECK(state.cursorDelta().y == 0.0);
    }

    TEST_CASE("sums the cursor movement of a frame and clears it for the next")
    {
        InputState state;
        state.placeCursor({ 10.0, 10.0 });
        state.beginFrame();
        state.apply(moveTo(14.0, 10.0));
        state.apply(moveTo(15.0, 7.0));

        CHECK(state.cursorDelta().x == 5.0);
        CHECK(state.cursorDelta().y == -3.0);
        CHECK(state.cursor().x == 15.0);

        state.beginFrame();
        CHECK(state.cursorDelta().x == 0.0);
        CHECK(state.cursor().x == 15.0);
    }

    TEST_CASE("sums the scroll of a frame and clears it for the next")
    {
        InputState state;
        state.beginFrame();
        state.apply(ScrollEvent{ 0.0, 1.0 });
        state.apply(ScrollEvent{ 0.5, 2.0 });

        CHECK(state.scroll().x == 0.5);
        CHECK(state.scroll().y == 3.0);

        state.beginFrame();
        CHECK(state.scroll().y == 0.0);
    }

    TEST_CASE("takes the modifiers from the last key event")
    {
        InputState state;
        state.beginFrame();
        state.apply(down(Key::A, Modifiers::Control | Modifiers::Shift));
        CHECK(state.modifiers() == (Modifiers::Control | Modifiers::Shift));

        state.apply(up(Key::A));
        CHECK(state.modifiers() == Modifiers::None);
    }

    TEST_CASE("reports no keyboard input while the keyboard is captured")
    {
        InputState state;
        state.beginFrame();
        state.apply(down(Key::W, Modifiers::Shift));
        state.setCaptured(true, false);

        CHECK(state.keyboardCaptured());
        CHECK_FALSE(state.keyDown(Key::W));
        CHECK_FALSE(state.keyPressed(Key::W));
        CHECK(state.modifiers() == Modifiers::None);

        state.setCaptured(false, false);
        CHECK(state.keyDown(Key::W)); // Still held underneath
    }

    TEST_CASE("reports no mouse input while the mouse is captured but keeps the cursor position")
    {
        InputState state;
        state.placeCursor({ 0.0, 0.0 });
        state.beginFrame();
        state.apply(click(MouseButton::Left));
        state.apply(moveTo(8.0, 9.0));
        state.apply(ScrollEvent{ 0.0, 1.0 });
        state.apply(down(Key::W));
        state.setCaptured(false, true);

        CHECK(state.mouseCaptured());
        CHECK_FALSE(state.mouseDown(MouseButton::Left));
        CHECK(state.cursorDelta().x == 0.0);
        CHECK(state.scroll().y == 0.0);
        CHECK(state.cursor().x == 8.0);
        CHECK(state.keyDown(Key::W)); // The keyboard is not captured
    }
}
