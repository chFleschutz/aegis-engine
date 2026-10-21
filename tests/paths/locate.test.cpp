#include <doctest/doctest.h>

import std;
import aegis.paths;

TEST_SUITE("paths::locate")
{
    using aegis::paths::locate;
    using aegis::paths::Lookup;
    using std::filesystem::path;

    const path given{ "given" };
    const path beside{ "exe/project" };
    const path configured{ "source/project" };

    // A fake file system: only the folders listed exist.
    auto existing(std::initializer_list<path> folders) -> std::function<bool(const path&)>
    {
        return [list = std::vector<path>(folders)](const path& folder) {
            return std::ranges::find(list, folder) != list.end();
        };
    }

    TEST_CASE("a given folder wins over the others")
    {
        const auto found = locate({ .given = given, .besideExecutable = beside, .configured = configured },
            existing({ given, beside, configured }));
        CHECK(found == given);
    }

    TEST_CASE("a given folder that is missing is an error, not a fallback")
    {
        const auto found = locate({ .given = given, .besideExecutable = beside, .configured = configured },
            existing({ beside, configured }));
        REQUIRE_FALSE(found);
        CHECK(found.error().find("given") != std::string::npos);
    }

    TEST_CASE("the folder next to the executable comes before the configured one")
    {
        const auto found = locate({ .besideExecutable = beside, .configured = configured }, existing({ beside, configured }));
        CHECK(found == beside);
    }

    TEST_CASE("the configured folder is the last resort")
    {
        const auto found = locate({ .besideExecutable = beside, .configured = configured }, existing({ configured }));
        CHECK(found == configured);
    }

    TEST_CASE("without a configured folder only the one next to the executable counts")
    {
        const auto found = locate({ .besideExecutable = beside }, existing({ configured }));
        CHECK_FALSE(found);
    }

    TEST_CASE("the error names every folder tried")
    {
        const auto found = locate({ .besideExecutable = beside, .configured = configured }, existing({}));
        REQUIRE_FALSE(found);
        CHECK(found.error().find(beside.string()) != std::string::npos);
        CHECK(found.error().find(configured.string()) != std::string::npos);
    }

    TEST_CASE("an empty lookup finds nothing")
    {
        CHECK_FALSE(locate({}, existing({ given })));
    }
}
