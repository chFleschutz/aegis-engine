#include <doctest/doctest.h>

import std;
import aegis.editor.core;

namespace
{
using aegis::editor::SideRail;
}

TEST_SUITE("editor::SideRail")
{
    TEST_CASE("starts unfolded with no flyout")
    {
        const SideRail rail;
        CHECK_FALSE(rail.folded());
        CHECK_FALSE(rail.flyout());
    }

    TEST_CASE("shows no flyout while unfolded")
    {
        SideRail rail;
        CHECK_FALSE(rail.show("Hierarchy"));
        CHECK_FALSE(rail.toggle("Inspector"));
        CHECK_FALSE(rail.flyout());
    }

    TEST_CASE("shows one panel at a time while folded")
    {
        SideRail rail;
        rail.toggleFolded();
        CHECK(rail.show("Hierarchy"));
        CHECK(rail.show("Inspector"));
        CHECK(rail.flyout() == "Inspector");
        CHECK_FALSE(rail.isShown("Hierarchy"));
    }

    TEST_CASE("toggling the shown panel closes the flyout and any other shows it")
    {
        SideRail rail;
        rail.setFolded(true);
        rail.toggle("Hierarchy");
        CHECK(rail.isShown("Hierarchy"));
        rail.toggle("Inspector");
        CHECK(rail.isShown("Inspector"));
        rail.toggle("Inspector");
        CHECK_FALSE(rail.flyout());
    }

    TEST_CASE("unfolding closes the flyout")
    {
        SideRail rail;
        rail.setFolded(true);
        rail.show("Inspector");
        rail.toggleFolded();
        CHECK_FALSE(rail.folded());
        CHECK_FALSE(rail.flyout());
    }

    TEST_CASE("keeps the folded state in its settings line")
    {
        SideRail folded;
        folded.setFolded(true);
        folded.show("Hierarchy"); // Not kept
        SideRail read;
        read.readSettingsLine(folded.settingsLine());
        CHECK(read.folded());
        CHECK_FALSE(read.flyout());

        read.readSettingsLine(SideRail{}.settingsLine());
        CHECK_FALSE(read.folded());
    }

    TEST_CASE("ignores lines it did not write")
    {
        SideRail rail;
        rail.setFolded(true);
        rail.readSettingsLine("Pinned=Assets");
        rail.readSettingsLine("Folded=yes");
        rail.readSettingsLine("");
        CHECK(rail.folded());
    }
}
