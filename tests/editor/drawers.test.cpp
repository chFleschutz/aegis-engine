#include <doctest/doctest.h>

import std;
import aegis.editor.core;

namespace
{
using aegis::editor::Drawers;
}

TEST_SUITE("editor::Drawers")
{
    TEST_CASE("starts closed with nothing pinned")
    {
        const Drawers drawers;
        CHECK_FALSE(drawers.shown());
        CHECK(drawers.pinned().empty());
    }

    TEST_CASE("shows one panel at a time")
    {
        Drawers drawers;
        CHECK(drawers.show("Assets"));
        CHECK(drawers.show("Console"));
        CHECK(drawers.shown() == "Console");
        CHECK_FALSE(drawers.isShown("Assets"));
    }

    TEST_CASE("toggling the shown panel closes the drawer and any other shows it")
    {
        Drawers drawers;
        drawers.toggle("Assets");
        CHECK(drawers.isShown("Assets"));
        drawers.toggle("Console");
        CHECK(drawers.isShown("Console"));
        drawers.toggle("Console");
        CHECK_FALSE(drawers.shown());
    }

    TEST_CASE("hiding a panel closes the drawer only when it shows that panel")
    {
        Drawers drawers;
        drawers.show("Assets");
        drawers.hide("Console");
        CHECK(drawers.isShown("Assets"));
        drawers.hide("Assets");
        CHECK_FALSE(drawers.shown());
    }

    TEST_CASE("pinning the shown panel closes the drawer")
    {
        Drawers drawers;
        drawers.show("Assets");
        drawers.pin("Assets");
        CHECK(drawers.isPinned("Assets"));
        CHECK_FALSE(drawers.shown());
    }

    TEST_CASE("pins the shown panel by the name the drawer shows")
    {
        Drawers drawers;
        drawers.show("Assets");
        drawers.pin(*drawers.shown());
        CHECK(drawers.isPinned("Assets"));
        CHECK_FALSE(drawers.shown());
    }

    TEST_CASE("a pinned panel does not show in the drawer")
    {
        Drawers drawers;
        drawers.show("Console");
        drawers.pin("Assets");
        CHECK_FALSE(drawers.show("Assets"));
        CHECK_FALSE(drawers.toggle("Assets"));
        CHECK(drawers.isShown("Console"));
    }

    TEST_CASE("unpinning returns the panel to the drawer, which shows it")
    {
        Drawers drawers;
        drawers.pin("Assets");
        drawers.pin("Console");
        drawers.unpin("Assets");
        CHECK_FALSE(drawers.isPinned("Assets"));
        CHECK(drawers.isPinned("Console"));
        CHECK(drawers.isShown("Assets"));
    }

    TEST_CASE("reset unpins every panel and closes the drawer")
    {
        Drawers drawers;
        drawers.pin("Assets");
        drawers.show("Console");
        drawers.reset();
        CHECK(drawers.pinned().empty());
        CHECK_FALSE(drawers.shown());
    }

    TEST_CASE("pins a panel once")
    {
        Drawers drawers;
        drawers.pin("Assets");
        drawers.pin("Assets");
        CHECK(drawers.pinned().size() == 1);
    }

    TEST_CASE("writes the pinned panels in the order they were pinned")
    {
        Drawers drawers;
        CHECK(drawers.settingsLine() == "Pinned=");
        drawers.pin("Console");
        drawers.pin("Assets");
        CHECK(drawers.settingsLine() == "Pinned=Console,Assets");
    }

    TEST_CASE("reads back the pinned panels it wrote")
    {
        Drawers saved;
        saved.pin("Assets");
        saved.pin("Profiler");
        Drawers loaded;
        loaded.readSettingsLine(saved.settingsLine());
        CHECK(std::ranges::equal(loaded.pinned(), saved.pinned()));
    }

    TEST_CASE("ignores other lines, empty names and repeated names when reading")
    {
        Drawers drawers;
        drawers.pin("Console");
        drawers.readSettingsLine("Height=3");
        CHECK(drawers.isPinned("Console"));
        drawers.readSettingsLine("Pinned=,Assets,,Assets");
        CHECK(drawers.pinned().size() == 1);
        CHECK(drawers.isPinned("Assets"));
        drawers.readSettingsLine("Pinned=");
        CHECK(drawers.pinned().empty());
    }
}
