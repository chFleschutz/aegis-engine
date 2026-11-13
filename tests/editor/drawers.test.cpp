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
        CHECK(drawers.show("Content"));
        CHECK(drawers.show("Console"));
        CHECK(drawers.shown() == "Console");
        CHECK_FALSE(drawers.isShown("Content"));
    }

    TEST_CASE("toggling the shown panel closes the drawer and any other shows it")
    {
        Drawers drawers;
        drawers.toggle("Content");
        CHECK(drawers.isShown("Content"));
        drawers.toggle("Console");
        CHECK(drawers.isShown("Console"));
        drawers.toggle("Console");
        CHECK_FALSE(drawers.shown());
    }

    TEST_CASE("hiding a panel closes the drawer only when it shows that panel")
    {
        Drawers drawers;
        drawers.show("Content");
        drawers.hide("Console");
        CHECK(drawers.isShown("Content"));
        drawers.hide("Content");
        CHECK_FALSE(drawers.shown());
    }

    TEST_CASE("pinning the shown panel closes the drawer")
    {
        Drawers drawers;
        drawers.show("Content");
        drawers.pin("Content");
        CHECK(drawers.isPinned("Content"));
        CHECK_FALSE(drawers.shown());
    }

    TEST_CASE("pins the shown panel by the name the drawer shows")
    {
        Drawers drawers;
        drawers.show("Content");
        drawers.pin(*drawers.shown());
        CHECK(drawers.isPinned("Content"));
        CHECK_FALSE(drawers.shown());
    }

    TEST_CASE("a pinned panel does not show in the drawer")
    {
        Drawers drawers;
        drawers.show("Console");
        drawers.pin("Content");
        CHECK_FALSE(drawers.show("Content"));
        CHECK_FALSE(drawers.toggle("Content"));
        CHECK(drawers.isShown("Console"));
    }

    TEST_CASE("unpinning returns the panel to the drawer, which shows it")
    {
        Drawers drawers;
        drawers.pin("Content");
        drawers.pin("Console");
        drawers.unpin("Content");
        CHECK_FALSE(drawers.isPinned("Content"));
        CHECK(drawers.isPinned("Console"));
        CHECK(drawers.isShown("Content"));
    }

    TEST_CASE("reset unpins every panel and closes the drawer")
    {
        Drawers drawers;
        drawers.pin("Content");
        drawers.show("Console");
        drawers.reset();
        CHECK(drawers.pinned().empty());
        CHECK_FALSE(drawers.shown());
    }

    TEST_CASE("pins a panel once")
    {
        Drawers drawers;
        drawers.pin("Content");
        drawers.pin("Content");
        CHECK(drawers.pinned().size() == 1);
    }

    TEST_CASE("writes the pinned panels in the order they were pinned")
    {
        Drawers drawers;
        CHECK(drawers.settingsLine() == "Pinned=");
        drawers.pin("Console");
        drawers.pin("Content");
        CHECK(drawers.settingsLine() == "Pinned=Console,Content");
    }

    TEST_CASE("reads back the pinned panels it wrote")
    {
        Drawers saved;
        saved.pin("Content");
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
        drawers.readSettingsLine("Pinned=,Content,,Content");
        CHECK(drawers.pinned().size() == 1);
        CHECK(drawers.isPinned("Content"));
        drawers.readSettingsLine("Pinned=");
        CHECK(drawers.pinned().empty());
    }
}
