#include <doctest/doctest.h>

import std;
import aegis.asset_library;
import aegis.assets;
import aegis.core;

namespace
{
using namespace aegis;

// An asset type a game defines.
struct Dialogue
{
    static constexpr std::string_view assetType{ "Dialogue" };

    std::string text;
};

// Another C++ type that claims the same asset type name.
struct OtherDialogue
{
    static constexpr std::string_view assetType{ "Dialogue" };
};

constexpr assets::AssetId hello{ 0x1111 };
constexpr assets::AssetId missing{ 0x2222 };

// Loads 'hello' and fails every other ID, counting its calls.
auto dialogueLoader(int& loads) -> AssetLoader<Dialogue>
{
    AssetLoader<Dialogue> loader;
    loader.load = [&loads](assets::AssetId id) -> std::expected<std::shared_ptr<Dialogue>, std::string> {
        ++loads;
        if (id != hello)
            return std::unexpected{ "no such line" };
        return std::make_shared<Dialogue>(Dialogue{ .text = "Hello" });
    };
    loader.fallback = [](assets::AssetId) { return std::make_shared<Dialogue>(Dialogue{ .text = "..." }); };
    return loader;
}

// Captures what the cache logs while it lives.
struct LogCapture
{
    std::vector<std::string> messages;

    LogCapture()
    {
        log::setSink([this](log::Level, std::string_view message) { messages.emplace_back(message); });
    }
    ~LogCapture() { log::setSink({}); }
    LogCapture(const LogCapture&) = delete;
    auto operator=(const LogCapture&) -> LogCapture& = delete;
};
}

TEST_SUITE("AssetCache")
{
    TEST_CASE("A registered asset type loads by ID once")
    {
        AssetCache cache;
        int loads = 0;
        REQUIRE(cache.registerLoader(dialogueLoader(loads)));
        CHECK(cache.hasLoader<Dialogue>());

        const auto first = cache.load<Dialogue>(hello);
        REQUIRE(first.state() == assets::AssetState::Ready);
        CHECK(first->text == "Hello");
        CHECK(first.id() == hello);

        const auto second = cache.load<Dialogue>(hello);
        CHECK(second.slot() == first.slot());
        CHECK(loads == 1);
    }

    TEST_CASE("A failed load holds the fallback, is logged once and is not retried")
    {
        LogCapture log;
        AssetCache cache;
        int loads = 0;
        REQUIRE(cache.registerLoader(dialogueLoader(loads)));

        const auto failed = cache.load<Dialogue>(missing);
        CHECK(failed.state() == assets::AssetState::Failed);
        CHECK(failed.slot()->message == "no such line");
        REQUIRE(failed);
        CHECK(failed->text == "...");

        CHECK(cache.load<Dialogue>(missing).slot() == failed.slot());
        CHECK(loads == 1);
        REQUIRE(log.messages.size() == 1);
        CHECK(log.messages.front().starts_with("Failed to load Dialogue "));
        CHECK(log.messages.front().ends_with("no such line; using a fallback"));
    }

    TEST_CASE("A loader that logs no failures fails silently")
    {
        LogCapture log;
        AssetCache cache;
        int loads = 0;
        auto loader = dialogueLoader(loads);
        loader.logFailures = false;
        REQUIRE(cache.registerLoader(std::move(loader)));

        CHECK(cache.load<Dialogue>(missing).state() == assets::AssetState::Failed);
        CHECK(log.messages.empty());
    }

    TEST_CASE("An asset type without a loader fails to load until one is registered")
    {
        LogCapture log;
        AssetCache cache;
        CHECK_FALSE(cache.hasLoader<Dialogue>());

        const auto ref = cache.load<Dialogue>(hello);
        CHECK(ref.state() == assets::AssetState::Failed);
        CHECK_FALSE(ref);
        CHECK(ref.slot()->message == "no asset loader for the asset type 'Dialogue'");
        CHECK(log.messages.size() == 1);
        CHECK_FALSE(cache.find<Dialogue>(hello));

        int loads = 0;
        REQUIRE(cache.registerLoader(dialogueLoader(loads)));
        CHECK(cache.load<Dialogue>(hello)->text == "Hello");
    }

    TEST_CASE("A loader that is not ready fails without keeping the failure")
    {
        AssetCache cache;
        int loads = 0;
        bool ready = false;
        auto loader = dialogueLoader(loads);
        loader.ready = [&ready] { return ready; };
        REQUIRE(cache.registerLoader(std::move(loader)));

        const auto early = cache.load<Dialogue>(hello);
        CHECK(early.state() == assets::AssetState::Failed);
        CHECK_FALSE(early);
        CHECK_FALSE(cache.find<Dialogue>(hello));
        CHECK(loads == 0);

        ready = true;
        CHECK(cache.load<Dialogue>(hello)->text == "Hello");
        CHECK(loads == 1);
    }

    TEST_CASE("Registering an asset type twice fails and keeps the first loader")
    {
        AssetCache cache;
        int firstLoads = 0;
        int secondLoads = 0;
        REQUIRE(cache.registerLoader(dialogueLoader(firstLoads)));

        const auto again = cache.registerLoader(dialogueLoader(secondLoads));
        REQUIRE_FALSE(again);
        CHECK(again.error().code == AssetLibraryErrorCode::AlreadyRegistered);

        const auto other = cache.registerLoader(AssetLoader<OtherDialogue>{});
        REQUIRE_FALSE(other);
        CHECK(other.error().code == AssetLibraryErrorCode::AlreadyRegistered);
        CHECK_FALSE(cache.hasLoader<OtherDialogue>());

        (void)cache.load<Dialogue>(hello);
        CHECK(firstLoads == 1);
        CHECK(secondLoads == 0);
    }

    TEST_CASE("A filled slot is found and loads without the loader")
    {
        AssetCache cache;
        int loads = 0;
        REQUIRE(cache.registerLoader(dialogueLoader(loads)));

        CHECK_FALSE(cache.find<Dialogue>(missing));
        const auto filled = cache.fill(missing, std::make_shared<Dialogue>(Dialogue{ .text = "Made at run time" }));
        CHECK(filled.state() == assets::AssetState::Ready);

        const auto found = cache.find<Dialogue>(missing);
        REQUIRE(found);
        CHECK(found->slot() == filled.slot());
        CHECK(cache.load<Dialogue>(missing)->text == "Made at run time");
        CHECK(loads == 0);
    }

    TEST_CASE("A loader may fill the slots of the parts it makes")
    {
        AssetCache cache;
        constexpr assets::AssetId part{ 0x3333 };
        AssetLoader<Dialogue> loader;
        loader.load = [&cache, part](assets::AssetId id) -> std::expected<std::shared_ptr<Dialogue>, std::string> {
            if (id == part)
                return std::unexpected{ "parts load with their owner" };
            (void)cache.fill(part, std::make_shared<Dialogue>(Dialogue{ .text = "Part" }));
            return std::make_shared<Dialogue>(Dialogue{ .text = "Owner" });
        };
        REQUIRE(cache.registerLoader(std::move(loader)));

        CHECK(cache.load<Dialogue>(hello)->text == "Owner");
        const auto loadedPart = cache.load<Dialogue>(part);
        CHECK(loadedPart.state() == assets::AssetState::Ready);
        CHECK(loadedPart->text == "Part");
    }

    TEST_CASE("One ID loads as two asset types into two slots")
    {
        AssetCache cache;
        int loads = 0;
        REQUIRE(cache.registerLoader(dialogueLoader(loads)));
        (void)cache.fill(hello, std::make_shared<assets::AudioClip>());

        CHECK(cache.load<Dialogue>(hello)->text == "Hello");
        CHECK(cache.find<assets::AudioClip>(hello));
        CHECK(loads == 1);
    }
}
