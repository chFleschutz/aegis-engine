#include <doctest/doctest.h>

import std;
import aegis.base;
import aegis.render_graph;
import aegis.rhi;

namespace
{
using namespace aegis::render_graph;
using aegis::rhi::Format;

constexpr TextureDesc color{ .format = Format::RGBA8_UNORM };
constexpr BufferDesc small{ .size = 64 };

// The message of a plan that must have failed with InvalidGraph.
auto messageOf(const std::expected<GraphPlan, Error>& plan) -> std::string
{
    REQUIRE_FALSE(plan.has_value());
    CHECK(plan.error().code == ErrorCode::InvalidGraph);
    return plan.error().message;
}
}

TEST_SUITE("render_graph::ResourceDeclarations")
{
    TEST_CASE("derives what a pass reads and writes from its declarations")
    {
        ResourceDeclarations declarations;
        declarations.beginPass("Producer");
        const auto image = declarations.addTexture("Image", Usage::ColorAttachment, color);
        const auto uniforms = declarations.addBuffer("Uniforms", Usage::HostWrite, small);
        declarations.beginPass("Consumer");
        const auto sampled = declarations.addReference("Image", Usage::SampledFragment);
        const auto written = declarations.addReference("Uniforms", Usage::StorageWrite);
        const auto read = declarations.addReference("Uniforms", Usage::ShaderRead);

        CHECK(declarations.passCount() == 2);
        CHECK(declarations.passName(1) == "Consumer");
        CHECK(declarations.reads(0).empty());
        CHECK(declarations.writes(0) == std::vector{ image, uniforms });
        CHECK(declarations.reads(1) == std::vector{ sampled, read });
        CHECK(declarations.writes(1) == std::vector{ written });
    }

    TEST_CASE("treats creating a resource as a write whatever its usage")
    {
        ResourceDeclarations declarations;
        declarations.beginPass("Producer");
        const auto image = declarations.addTexture("Image", Usage::SampledFragment, color);

        CHECK(declarations.reads(0).empty());
        CHECK(declarations.writes(0) == std::vector{ image });
    }
}

TEST_SUITE("render_graph::compilePlan")
{
    using aegis::render_graph::detail::compilePlan;

    TEST_CASE("rejects two passes creating the same name, naming both passes")
    {
        ResourceDeclarations declarations;
        declarations.beginPass("First");
        declarations.addTexture("Shared", Usage::ColorAttachment, color);
        declarations.beginPass("Second");
        declarations.addTexture("Shared", Usage::ColorAttachment, color);

        const auto message = messageOf(compilePlan(declarations));
        CHECK(message.contains("'Shared'"));
        CHECK(message.contains("'First'"));
        CHECK(message.contains("'Second'"));
    }

    TEST_CASE("rejects a reference to a resource nobody creates, naming the pass")
    {
        ResourceDeclarations declarations;
        declarations.beginPass("Reader");
        declarations.addReference("Missing", Usage::SampledFragment);

        const auto message = messageOf(compilePlan(declarations));
        CHECK(message.contains("'Missing'"));
        CHECK(message.contains("'Reader'"));
    }

    TEST_CASE("resolves a reference to the pass creating the resource")
    {
        ResourceDeclarations declarations;
        declarations.beginPass("Reader");
        declarations.addReference("Image", Usage::SampledFragment); // Entry 0, declared before its creator
        declarations.beginPass("Producer");
        declarations.addTexture("Other", Usage::ColorAttachment, color);
        declarations.addTexture("Image", Usage::ColorAttachment, color); // Entry 2

        const auto plan = compilePlan(declarations);
        REQUIRE(plan.has_value());
        CHECK(plan->target == std::vector<std::uint32_t>{ 2, 1, 2 });
        CHECK(plan->order == std::vector<std::uint32_t>{ 1, 0 });
    }

    TEST_CASE("rejects a buffer usage on a texture and a texture usage on a buffer")
    {
        ResourceDeclarations onTexture;
        onTexture.beginPass("Producer");
        onTexture.addTexture("Image", Usage::ColorAttachment, color);
        onTexture.beginPass("Reader");
        onTexture.addReference("Image", Usage::ShaderRead);
        const auto textureMessage = messageOf(compilePlan(onTexture));
        CHECK(textureMessage.contains("'Reader'"));
        CHECK(textureMessage.contains("'Image'"));
        CHECK(textureMessage.contains("ShaderRead"));

        ResourceDeclarations onBuffer;
        onBuffer.beginPass("Producer");
        onBuffer.addBuffer("Data", Usage::StorageWrite, small);
        onBuffer.beginPass("Writer");
        onBuffer.addReference("Data", Usage::ColorAttachment);
        const auto bufferMessage = messageOf(compilePlan(onBuffer));
        CHECK(bufferMessage.contains("'Writer'"));
        CHECK(bufferMessage.contains("'Data'"));
        CHECK(bufferMessage.contains("ColorAttachment"));
    }

    TEST_CASE("rejects a pass accessing one texture in two layouts")
    {
        ResourceDeclarations declarations;
        declarations.beginPass("Producer");
        declarations.addTexture("Image", Usage::ColorAttachment, color);
        declarations.beginPass("Blur");
        declarations.addReference("Image", Usage::SampledCompute);
        declarations.addReference("Image", Usage::StorageWrite);

        const auto message = messageOf(compilePlan(declarations));
        CHECK(message.contains("'Blur'"));
        CHECK(message.contains("'Image'"));
    }

    TEST_CASE("accepts a pass accessing one texture twice in the same layout")
    {
        ResourceDeclarations declarations;
        declarations.beginPass("Producer");
        declarations.addTexture("Image", Usage::StorageWrite, color);
        declarations.beginPass("InPlace");
        declarations.addReference("Image", Usage::StorageRead);
        declarations.addReference("Image", Usage::StorageWrite);

        CHECK(compilePlan(declarations).has_value());
    }

    TEST_CASE("reports a dependency cycle")
    {
        ResourceDeclarations declarations;
        declarations.beginPass("A");
        declarations.addTexture("FromA", Usage::ColorAttachment, color);
        declarations.addReference("FromB", Usage::SampledFragment);
        declarations.beginPass("B");
        declarations.addTexture("FromB", Usage::ColorAttachment, color);
        declarations.addReference("FromA", Usage::SampledFragment);

        const auto message = messageOf(compilePlan(declarations));
        CHECK(message.contains("cycle"));
    }

    TEST_CASE("runs a reader declared before the next writer ahead of it")
    {
        ResourceDeclarations declarations;
        declarations.beginPass("Producer"); // 0
        declarations.addTexture("Image", Usage::StorageWrite, color);
        declarations.beginPass("Reader"); // 1, declared before the overwriter
        declarations.addReference("Image", Usage::SampledCompute);
        declarations.beginPass("Overwriter"); // 2
        declarations.addReference("Image", Usage::StorageWrite);

        const auto plan = compilePlan(declarations);
        REQUIRE(plan.has_value());
        CHECK(plan->order == std::vector<std::uint32_t>{ 0, 1, 2 });
    }

    TEST_CASE("merges the usage flags of every access into the resource")
    {
        using aegis::base::hasFlag;
        using aegis::rhi::BufferUsage;
        using aegis::rhi::ImageUsage;

        ResourceDeclarations declarations;
        declarations.beginPass("Producer");
        declarations.addTexture("Image", Usage::ColorAttachment, { .format = Format::RGBA8_UNORM, .usage = ImageUsage::TransferSrc }); // Entry 0
        declarations.addBuffer("Uniforms", Usage::HostWrite, small);                                                                   // Entry 1
        declarations.beginPass("Consumer");
        declarations.addReference("Image", Usage::SampledFragment); // Entry 2
        declarations.addReference("Uniforms", Usage::IndirectRead); // Entry 3

        const auto plan = compilePlan(declarations);
        REQUIRE(plan.has_value());
        CHECK(hasFlag(plan->imageUsage[0], ImageUsage::ColorAttachment));
        CHECK(hasFlag(plan->imageUsage[0], ImageUsage::Sampled));
        CHECK(hasFlag(plan->imageUsage[0], ImageUsage::TransferSrc));
        CHECK_FALSE(hasFlag(plan->imageUsage[0], ImageUsage::Storage));
        CHECK(hasFlag(plan->bufferUsage[1], BufferUsage::Indirect));
        CHECK(plan->hostWritten[1]);
        CHECK_FALSE(plan->hostWritten[0]);
    }

    TEST_CASE("plans the barriers between the passes from the declared usages")
    {
        using aegis::render_graph::detail::Transition;

        ResourceDeclarations declarations;
        declarations.beginPass("Producer");
        declarations.addTexture("Image", Usage::ColorAttachment, color);
        declarations.beginPass("Reader");
        declarations.addReference("Image", Usage::SampledFragment);

        const auto plan = compilePlan(declarations);
        REQUIRE(plan.has_value());
        REQUIRE(plan->transitions.size() == 2);
        CHECK(plan->transitions[0] == std::vector<Transition>{ { 0, std::nullopt, Usage::ColorAttachment } });
        CHECK(plan->transitions[1] == std::vector<Transition>{ { 0, Usage::ColorAttachment, Usage::SampledFragment } });
    }
}
