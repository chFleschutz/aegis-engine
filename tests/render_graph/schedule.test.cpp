#include <doctest/doctest.h>

import std;
import aegis.render_graph;

namespace
{
using aegis::render_graph::Lifetime;
using aegis::render_graph::Usage;
using namespace aegis::render_graph::detail;

auto created() -> EntryDecl
{
    return {};
}

auto referenceTo(std::uint32_t entry) -> EntryDecl
{
    return EntryDecl{ .referenceTo = entry };
}
}

TEST_SUITE("render_graph::sortPasses")
{
    TEST_CASE("a pass reading a resource runs after the pass creating it, whatever the add order")
    {
        // Pass 0 reads entry 1 (a reference to entry 0), which pass 1 creates.
        const std::vector entries{ created(), referenceTo(0) };
        const std::vector<PassDecl> passes{ { .reads = { 1 } }, { .writes = { 0 } } };

        const auto order = sortPasses(passes, entries);
        REQUIRE(order.has_value());
        CHECK(*order == std::vector<std::uint32_t>{ 1, 0 });
    }

    TEST_CASE("writers through references chain in add order, and later readers follow the last one")
    {
        // 0 creates the resource; 1 and 2 write it through references; 3 reads it.
        const std::vector entries{ created(), referenceTo(0), referenceTo(0), referenceTo(0) };
        const std::vector<PassDecl> passes{
            { .writes = { 0 } }, // Creator
            { .writes = { 1 } },
            { .writes = { 2 } },
            { .reads = { 3 } },
        };

        const auto order = sortPasses(passes, entries);
        REQUIRE(order.has_value());
        CHECK(*order == std::vector<std::uint32_t>{ 0, 1, 2, 3 });
    }

    TEST_CASE("writers declared out of creation order still run after the creator")
    {
        const std::vector entries{ created(), referenceTo(0) };
        const std::vector<PassDecl> passes{ { .writes = { 1 } }, { .writes = { 0 } } };

        const auto order = sortPasses(passes, entries);
        REQUIRE(order.has_value());
        CHECK(*order == std::vector<std::uint32_t>{ 1, 0 });
    }

    TEST_CASE("independent passes keep their add order")
    {
        const std::vector entries{ created(), created(), created() };
        const std::vector<PassDecl> passes{ { .writes = { 0 } }, { .writes = { 1 } }, { .writes = { 2 } } };

        const auto order = sortPasses(passes, entries);
        REQUIRE(order.has_value());
        CHECK(*order == std::vector<std::uint32_t>{ 0, 1, 2 });
    }

    TEST_CASE("a reader declared before a writer runs before it")
    {
        // A (0) creates X, B (1) reads it, C (2) writes it in place.
        const std::vector entries{ created(), referenceTo(0), referenceTo(0) };
        const std::vector<PassDecl> passes{ { .writes = { 0 } }, { .reads = { 1 } }, { .writes = { 2 } } };

        const auto order = sortPasses(passes, entries);
        REQUIRE(order.has_value());
        CHECK(*order == std::vector<std::uint32_t>{ 0, 1, 2 });
    }

    TEST_CASE("a pass reading and writing a resource follows earlier readers and writers")
    {
        // 0 creates X; 1 reads it; 2 reads and writes it; 3 reads the result.
        const std::vector entries{ created(), referenceTo(0), referenceTo(0), referenceTo(0) };
        const std::vector<PassDecl> passes{
            { .reads = { 3 } },
            { .writes = { 0 } },
            { .reads = { 1 }, .writes = { 2 } },
            { .reads = { 3 } },
        };

        const auto order = sortPasses(passes, entries);
        REQUIRE(order.has_value());
        CHECK(*order == std::vector<std::uint32_t>{ 1, 0, 2, 3 });
    }

    TEST_CASE("reports a dependency cycle")
    {
        // Pass 0 creates entry 0 and reads entry 3 (pass 1's); pass 1 the other way round.
        const std::vector entries{ created(), referenceTo(0), created(), referenceTo(2) };
        const std::vector<PassDecl> passes{
            { .reads = { 3 }, .writes = { 0 } },
            { .reads = { 1 }, .writes = { 2 } },
        };
        CHECK(!sortPasses(passes, entries).has_value());
    }
}

TEST_SUITE("render_graph::planTransitions")
{
    const std::vector<PlannedResource> kinds{ { .kind = ResourceKind::Image }, { .kind = ResourceKind::Buffer } };
    constexpr std::uint32_t image{ 0 };
    constexpr std::uint32_t buffer{ 1 };

    TEST_CASE("an image's first use each frame transitions from undefined")
    {
        const std::vector<std::vector<Access>> passes{ { { image, Usage::ColorAttachment } } };
        const auto transitions = planTransitions(passes, kinds);
        REQUIRE(transitions[0].size() == 1);
        CHECK(transitions[0][0] == Transition{ image, std::nullopt, Usage::ColorAttachment });
    }

    TEST_CASE("a read after a write gets a barrier")
    {
        const std::vector<std::vector<Access>> passes{
            { { image, Usage::StorageWrite } },
            { { image, Usage::SampledFragment } },
        };
        const auto transitions = planTransitions(passes, kinds);
        REQUIRE(transitions[1].size() == 1);
        CHECK(transitions[1][0] == Transition{ image, Usage::StorageWrite, Usage::SampledFragment });
    }

    TEST_CASE("reads in the same layout need no barrier")
    {
        const std::vector<std::vector<Access>> passes{
            { { image, Usage::ColorAttachment } },
            { { image, Usage::SampledFragment } },
            { { image, Usage::SampledCompute } }, // Same read-only layout
        };
        const auto transitions = planTransitions(passes, kinds);
        CHECK(transitions[2].empty());
    }

    TEST_CASE("a read in another layout needs a barrier")
    {
        const std::vector<std::vector<Access>> passes{
            { { image, Usage::ColorAttachment } },
            { { image, Usage::SampledFragment } },
            { { image, Usage::SampledCompute } },
            { { image, Usage::StorageRead } }, // General layout
        };
        const auto transitions = planTransitions(passes, kinds);
        REQUIRE(transitions[3].size() == 1);
        CHECK(transitions[3][0] == Transition{ image, Usage::SampledCompute, Usage::StorageRead });
    }

    TEST_CASE("a buffer's first use syncs with its last use of the previous frame")
    {
        const std::vector<std::vector<Access>> passes{
            { { buffer, Usage::TransferDst } },
            { { buffer, Usage::StorageWrite } },
            { { buffer, Usage::IndirectRead } },
        };
        const auto transitions = planTransitions(passes, kinds);
        REQUIRE(transitions[0].size() == 1);
        CHECK(transitions[0][0] == Transition{ .resource = buffer, .from = Usage::IndirectRead, .to = Usage::TransferDst, .crossFrame = true });
        CHECK(transitions[1][0] == Transition{ buffer, Usage::TransferDst, Usage::StorageWrite });
        CHECK(transitions[2][0] == Transition{ buffer, Usage::StorageWrite, Usage::IndirectRead });
    }

    TEST_CASE("a buffer only ever read needs no barrier")
    {
        const std::vector<std::vector<Access>> passes{
            { { buffer, Usage::ShaderRead } },
            { { buffer, Usage::StorageRead } },
        };
        const auto transitions = planTransitions(passes, kinds);
        CHECK(transitions[0].empty());
        CHECK(transitions[1].empty());
    }

    TEST_CASE("host writes are not GPU accesses")
    {
        const std::vector<std::vector<Access>> passes{
            { { buffer, Usage::HostWrite } },
            { { buffer, Usage::ShaderRead } },
        };
        const auto transitions = planTransitions(passes, kinds);
        CHECK(transitions[0].empty());
        CHECK(transitions[1].empty());
    }

    TEST_CASE("a persistent texture's first use syncs with its last use of the previous frame")
    {
        const std::vector<PlannedResource> resources{ { .kind = ResourceKind::Image, .lifetime = Lifetime::Persistent } };
        const std::vector<std::vector<Access>> passes{
            { { 0, Usage::SampledFragment } },
            { { 0, Usage::StorageWrite } },
        };
        const auto transitions = planTransitions(passes, resources);
        REQUIRE(transitions[0].size() == 1);
        CHECK(transitions[0][0] == Transition{ .resource = 0, .from = Usage::StorageWrite, .to = Usage::SampledFragment, .crossFrame = true });
        CHECK(transitions[1] == std::vector<Transition>{ { 0, Usage::SampledFragment, Usage::StorageWrite } });
    }

    TEST_CASE("a persistent texture only ever read still gets its first barrier, for the frame it is fresh in")
    {
        const std::vector<PlannedResource> resources{ { .kind = ResourceKind::Image, .lifetime = Lifetime::Persistent } };
        const std::vector<std::vector<Access>> passes{ { { 0, Usage::SampledFragment } } };
        const auto transitions = planTransitions(passes, resources);
        CHECK(transitions[0] == std::vector<Transition>{ { .resource = 0, .from = Usage::SampledFragment, .to = Usage::SampledFragment, .crossFrame = true } });
    }

    TEST_CASE("the images of a history pair each sync with the other's last use")
    {
        const std::vector<PlannedResource> resources{ { .kind = ResourceKind::Image, .lifetime = Lifetime::History } };
        // One pass reads the previous frame's image and writes the current one; a second reads the current.
        const std::vector<std::vector<Access>> passes{
            { { 0, Usage::SampledCompute, true }, { 0, Usage::StorageWrite } },
            { { 0, Usage::SampledFragment } },
        };
        const auto transitions = planTransitions(passes, resources);
        REQUIRE(transitions[0].size() == 2);
        // The previous frame's image was the current one a frame ago, last sampled by the second pass.
        CHECK(transitions[0][0] == Transition{ .resource = 0, .from = Usage::SampledFragment, .to = Usage::SampledCompute, .previous = true, .crossFrame = true });
        // The current image was the previous frame's a frame ago, last sampled by the first pass.
        CHECK(transitions[0][1] == Transition{ .resource = 0, .from = Usage::SampledCompute, .to = Usage::StorageWrite, .crossFrame = true });
        CHECK(transitions[1] == std::vector<Transition>{ { 0, Usage::StorageWrite, Usage::SampledFragment } });
    }

    TEST_CASE("a history pair nobody reads the previous frame of starts each frame undefined")
    {
        const std::vector<PlannedResource> resources{ { .kind = ResourceKind::Image, .lifetime = Lifetime::History } };
        const std::vector<std::vector<Access>> passes{ { { 0, Usage::ColorAttachment } } };
        const auto transitions = planTransitions(passes, resources);
        CHECK(transitions[0] == std::vector<Transition>{ { 0, std::nullopt, Usage::ColorAttachment } });
    }

    TEST_CASE("an imported image's first use follows the state it is imported in")
    {
        const std::vector<PlannedResource> resources{
            { .kind = ResourceKind::Image, .imported = true, .before = Usage::ColorAttachment, .after = Usage::ColorAttachment },
            { .kind = ResourceKind::Image, .imported = true, .after = Usage::TransferSrc },
            { .kind = ResourceKind::Image, .imported = true, .before = Usage::SampledFragment, .after = Usage::SampledFragment },
        };
        const std::vector<std::vector<Access>> passes{
            { { 0, Usage::TransferDst }, { 1, Usage::TransferDst }, { 2, Usage::SampledCompute } },
        };
        const auto transitions = planTransitions(passes, resources);
        CHECK(transitions[0] == std::vector<Transition>{
                                    { 0, Usage::ColorAttachment, Usage::TransferDst },
                                    { 1, std::nullopt, Usage::TransferDst }, // No state: its contents are discarded
                                    // Resource 2 is read in the layout it arrives in: no barrier
                                });
    }

    TEST_CASE("an imported buffer syncs with the access before the frame, if it has one")
    {
        const std::vector<PlannedResource> resources{
            { .kind = ResourceKind::Buffer, .imported = true, .before = Usage::TransferDst },
            { .kind = ResourceKind::Buffer, .imported = true },
        };
        const std::vector<std::vector<Access>> passes{ { { 0, Usage::ShaderRead }, { 1, Usage::StorageWrite } } };
        const auto transitions = planTransitions(passes, resources);
        CHECK(transitions[0] == std::vector<Transition>{ { 0, Usage::TransferDst, Usage::ShaderRead } });
    }
}

TEST_SUITE("render_graph::planEndOfFrame")
{
    TEST_CASE("leaves each imported resource the frame accessed ready for its next access")
    {
        const std::vector<PlannedResource> resources{
            { .kind = ResourceKind::Image, .imported = true, .before = Usage::ColorAttachment, .after = Usage::ColorAttachment },
            { .kind = ResourceKind::Image, .imported = true, .after = Usage::SampledFragment },
            { .kind = ResourceKind::Buffer, .imported = true, .after = Usage::ShaderRead },
            { .kind = ResourceKind::Buffer, .imported = true },                             // Nothing follows the frame
            { .kind = ResourceKind::Image, .imported = true, .after = Usage::TransferSrc }, // Never accessed
            { .kind = ResourceKind::Image, .lifetime = Lifetime::Persistent },              // Not imported
        };
        const std::vector<std::vector<Access>> passes{
            { { 0, Usage::TransferDst }, { 1, Usage::SampledCompute }, { 2, Usage::StorageWrite }, { 3, Usage::StorageWrite }, { 5, Usage::StorageWrite } },
        };
        CHECK(planEndOfFrame(passes, resources) == std::vector<Transition>{
                                                       { 0, Usage::TransferDst, Usage::ColorAttachment },
                                                       // Resource 1 is already in the read-only layout
                                                       { 2, Usage::StorageWrite, Usage::ShaderRead },
                                                   });
    }
}
