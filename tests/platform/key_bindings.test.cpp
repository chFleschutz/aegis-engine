#include <doctest/doctest.h>

import std;
import aegis.platform;

namespace
{
using namespace aegis::platform;

constexpr auto press = KeyAction::Press;
constexpr auto none = Modifiers::None;
}

TEST_SUITE("platform::KeyBindings")
{
    TEST_CASE("calls a binding on its key, action and modifiers")
    {
        KeyBindings bindings;
        int calls = 0;
        bindings.bind(Key::K, [&] { ++calls; });

        bindings.dispatch(Key::K, press, none);
        CHECK(calls == 1);
    }

    TEST_CASE("ignores other keys, actions and modifiers")
    {
        KeyBindings bindings;
        int calls = 0;
        bindings.bind(Key::K, [&] { ++calls; });

        bindings.dispatch(Key::J, press, none);
        bindings.dispatch(Key::K, KeyAction::Release, none);
        bindings.dispatch(Key::K, KeyAction::Repeat, none);
        bindings.dispatch(Key::K, press, Modifiers::Control);
        CHECK(calls == 0);
    }

    TEST_CASE("matches the modifiers exactly")
    {
        KeyBindings bindings;
        int plain = 0;
        int ctrlShift = 0;
        bindings.bind(Key::S, [&] { ++plain; });
        bindings.bind(Key::S, [&] { ++ctrlShift; }, press, Modifiers::Control | Modifiers::Shift);

        bindings.dispatch(Key::S, press, Modifiers::Control);
        CHECK(plain == 0);
        CHECK(ctrlShift == 0);

        bindings.dispatch(Key::S, press, Modifiers::Control | Modifiers::Shift);
        CHECK(plain == 0);
        CHECK(ctrlShift == 1);
    }

    TEST_CASE("honours the bound action")
    {
        KeyBindings bindings;
        int releases = 0;
        bindings.bind(Key::K, [&] { ++releases; }, KeyAction::Release);

        bindings.dispatch(Key::K, press, none);
        CHECK(releases == 0);
        bindings.dispatch(Key::K, KeyAction::Release, none);
        CHECK(releases == 1);
    }

    TEST_CASE("runs every binding of a key in binding order")
    {
        KeyBindings bindings;
        std::vector<int> order;
        bindings.bind(Key::K, [&] { order.push_back(1); });
        bindings.bind(Key::K, [&] { order.push_back(2); });
        bindings.bind(Key::K, [&] { order.push_back(3); });

        bindings.dispatch(Key::K, press, none);
        CHECK(order == std::vector{ 1, 2, 3 });
    }

    TEST_CASE("gives each binding a distinct id")
    {
        KeyBindings bindings;
        const auto a = bindings.bind(Key::K, [] {});
        const auto b = bindings.bind(Key::K, [] {});
        CHECK(a != b);
    }

    TEST_CASE("stops calling an unbound binding and keeps the others")
    {
        KeyBindings bindings;
        int first = 0;
        int second = 0;
        const auto id = bindings.bind(Key::K, [&] { ++first; });
        bindings.bind(Key::K, [&] { ++second; });

        CHECK(bindings.unbind(id));
        bindings.dispatch(Key::K, press, none);
        CHECK(first == 0);
        CHECK(second == 1);
        CHECK(bindings.size() == 1);
    }

    TEST_CASE("reports unbinding an unknown or already removed id")
    {
        KeyBindings bindings;
        const auto id = bindings.bind(Key::K, [] {});

        CHECK(bindings.unbind(id));
        CHECK_FALSE(bindings.unbind(id));
        CHECK_FALSE(bindings.unbind(BindingId{ 12345 }));
    }

    TEST_CASE("does not reuse the id of an unbound binding")
    {
        KeyBindings bindings;
        const auto old = bindings.bind(Key::K, [] {});
        bindings.unbind(old);
        const auto fresh = bindings.bind(Key::K, [] {});

        CHECK(old != fresh);
        CHECK_FALSE(bindings.unbind(old));
        CHECK(bindings.size() == 1);
    }

    TEST_CASE("survives a callback that binds the same key")
    {
        KeyBindings bindings;
        int calls = 0;
        int added = 0;
        // Enough bindings that the first bind from inside the callback reallocates the storage.
        for (int i = 0; i < 4; ++i)
        {
            bindings.bind(Key::K, [&] {
                ++calls;
                bindings.bind(Key::K, [&] { ++added; });
            });
        }

        bindings.dispatch(Key::K, press, none);
        CHECK(calls == 4);
        CHECK(added == 0); // New bindings first see the next event
        CHECK(bindings.size() == 8);

        bindings.dispatch(Key::K, press, none);
        CHECK(added == 4);
    }

    TEST_CASE("survives a callback that unbinds itself")
    {
        KeyBindings bindings;
        int calls = 0;
        BindingId self;
        self = bindings.bind(Key::K, [&] {
            ++calls;
            bindings.unbind(self);
        });

        bindings.dispatch(Key::K, press, none);
        bindings.dispatch(Key::K, press, none);
        CHECK(calls == 1);
        CHECK(bindings.size() == 0);
    }

    TEST_CASE("does not call a binding that an earlier callback unbound")
    {
        KeyBindings bindings;
        int later = 0;
        BindingId laterId;
        bindings.bind(Key::K, [&] { bindings.unbind(laterId); });
        laterId = bindings.bind(Key::K, [&] { ++later; });

        bindings.dispatch(Key::K, press, none);
        CHECK(later == 0);
    }

    TEST_CASE("dispatching an unbound key does nothing")
    {
        const KeyBindings bindings;
        bindings.dispatch(Key::Unknown, press, none);
        CHECK(bindings.size() == 0);
    }
}
