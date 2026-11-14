#include <doctest/doctest.h>

import std;
import aegis.core;
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

    TEST_CASE("finds the declaration creating a resource, not a reference to it")
    {
        ResourceDeclarations declarations;
        declarations.beginPass("Consumer");
        declarations.addReference("Image", Usage::SampledFragment);
        declarations.beginPass("Producer");
        const auto image = declarations.addTexture("Image", Usage::ColorAttachment, color);

        CHECK(declarations.find("Image") == image);
        CHECK_FALSE(declarations.find("Missing").has_value());
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
        using aegis::core::hasFlag;
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

    TEST_CASE("rejects a history reference to a texture that is no history pair")
    {
        ResourceDeclarations declarations;
        declarations.beginPass("Producer");
        declarations.addTexture("Image", Usage::ColorAttachment, { .format = Format::RGBA8_UNORM, .lifetime = Lifetime::Persistent });
        declarations.beginPass("Reader");
        declarations.addHistoryReference("Image", Usage::SampledFragment);

        const auto message = messageOf(compilePlan(declarations));
        CHECK(message.contains("'Reader'"));
        CHECK(message.contains("'Image'"));
        CHECK(message.contains("Lifetime::History"));
    }

    TEST_CASE("rejects a history reference that writes")
    {
        ResourceDeclarations declarations;
        declarations.beginPass("Producer");
        declarations.addTexture("Image", Usage::ColorAttachment, { .format = Format::RGBA8_UNORM, .lifetime = Lifetime::History });
        declarations.addHistoryReference("Image", Usage::StorageWrite);

        const auto message = messageOf(compilePlan(declarations));
        CHECK(message.contains("'Producer'"));
        CHECK(message.contains("StorageWrite"));
    }

    TEST_CASE("a history reference orders no pass and merges its usage into the pair")
    {
        using aegis::core::hasFlag;
        using aegis::rhi::ImageUsage;

        // The reader is added first and reads the previous frame's image: it stays first.
        ResourceDeclarations declarations;
        declarations.beginPass("Reader");
        declarations.addHistoryReference("Image", Usage::SampledFragment);
        declarations.beginPass("Producer");
        declarations.addTexture("Image", Usage::ColorAttachment, { .format = Format::RGBA8_UNORM, .lifetime = Lifetime::History });

        const auto plan = compilePlan(declarations);
        REQUIRE(plan.has_value());
        CHECK(plan->order == std::vector<std::uint32_t>{ 0, 1 });
        CHECK(plan->previous == std::vector{ true, false });
        CHECK(hasFlag(plan->imageUsage[1], ImageUsage::Sampled));
        CHECK(hasFlag(plan->imageUsage[1], ImageUsage::ColorAttachment));
    }

    TEST_CASE("one pass may write a history pair's current image and read the previous frame's in another layout")
    {
        ResourceDeclarations declarations;
        declarations.beginPass("Accumulate");
        declarations.addTexture("Image", Usage::ColorAttachment, { .format = Format::RGBA8_UNORM, .lifetime = Lifetime::History });
        declarations.addHistoryReference("Image", Usage::SampledFragment);

        CHECK(compilePlan(declarations).has_value());
    }

    TEST_CASE("an imported texture or buffer is found by name and checked like a created one")
    {
        ResourceDeclarations declarations;
        declarations.beginPass("Importer");
        declarations.importTexture("Target", Usage::ColorAttachment, { .format = Format::RGBA8_UNORM, .after = Usage::ColorAttachment });
        declarations.importBuffer("Lights", Usage::ShaderRead, {});
        declarations.beginPass("Reader");
        declarations.addReference("Target", Usage::SampledFragment);

        CHECK(declarations.find("Target") == ResourceId{ 0 });
        const auto plan = compilePlan(declarations);
        REQUIRE(plan.has_value());
        CHECK(plan->order == std::vector<std::uint32_t>{ 0, 1 });
        CHECK(plan->kinds[1] == aegis::render_graph::detail::ResourceKind::Buffer);

        declarations.beginPass("Wrong");
        declarations.addReference("Lights", Usage::SampledFragment);
        CHECK(messageOf(compilePlan(declarations)).contains("'Lights'"));
    }
}

TEST_SUITE("render_graph::describePlan")
{
    using aegis::render_graph::detail::compilePlan;
    using aegis::render_graph::detail::describePlan;

    TEST_CASE("the plan of a persistent texture matches its snapshot")
    {
        ResourceDeclarations declarations;
        declarations.beginPass("Exposure");
        declarations.addReference("SceneColor", Usage::SampledCompute);
        declarations.addTexture("Luminance", Usage::StorageWrite, { .format = Format::R32_SFLOAT, .extent = { 1, 1 }, .lifetime = Lifetime::Persistent });
        declarations.beginPass("Scene");
        declarations.addTexture("SceneColor", Usage::ColorAttachment, color);
        declarations.beginPass("Tonemap");
        declarations.addReference("Luminance", Usage::SampledFragment);
        declarations.addReference("SceneColor", Usage::ColorAttachment);

        const auto plan = compilePlan(declarations);
        REQUIRE(plan.has_value());
        CHECK(describePlan(declarations, *plan) ==
              "1. Scene\n"
              "    create SceneColor ColorAttachment\n"
              "    barrier SceneColor: undefined -> ColorAttachment\n"
              "2. Exposure\n"
              "    use    SceneColor SampledCompute\n"
              "    create Luminance StorageWrite\n"
              "    barrier SceneColor: ColorAttachment -> SampledCompute\n"
              "    barrier Luminance: last frame's SampledFragment -> StorageWrite\n"
              "3. Tonemap\n"
              "    use    Luminance SampledFragment\n"
              "    use    SceneColor ColorAttachment\n"
              "    barrier Luminance: StorageWrite -> SampledFragment\n"
              "    barrier SceneColor: SampledCompute -> ColorAttachment\n"
              "resources\n"
              "    Luminance: texture R32_SFLOAT, persistent\n"
              "    SceneColor: texture RGBA8_UNORM\n");
    }

    TEST_CASE("the plan of a history pair matches its snapshot")
    {
        ResourceDeclarations declarations;
        declarations.beginPass("Scene");
        declarations.addTexture("SceneColor", Usage::ColorAttachment, color);
        declarations.beginPass("Resolve");
        declarations.addReference("SceneColor", Usage::SampledFragment);
        declarations.addHistoryReference("Resolved", Usage::SampledFragment);
        declarations.addTexture("Resolved", Usage::ColorAttachment, { .format = Format::RGBA8_UNORM, .lifetime = Lifetime::History });
        declarations.beginPass("Present");
        declarations.addReference("Resolved", Usage::TransferSrc);

        const auto plan = compilePlan(declarations);
        REQUIRE(plan.has_value());
        CHECK(describePlan(declarations, *plan) ==
              "1. Scene\n"
              "    create SceneColor ColorAttachment\n"
              "    barrier SceneColor: undefined -> ColorAttachment\n"
              "2. Resolve\n"
              "    use    SceneColor SampledFragment\n"
              "    use    Resolved SampledFragment (previous frame)\n"
              "    create Resolved ColorAttachment\n"
              "    barrier SceneColor: ColorAttachment -> SampledFragment\n"
              "    barrier Resolved (previous frame): last frame's TransferSrc -> SampledFragment\n"
              "    barrier Resolved: last frame's SampledFragment -> ColorAttachment\n"
              "3. Present\n"
              "    use    Resolved TransferSrc\n"
              "    barrier Resolved: ColorAttachment -> TransferSrc\n"
              "resources\n"
              "    SceneColor: texture RGBA8_UNORM\n"
              "    Resolved: texture RGBA8_UNORM, history pair\n");
    }

    TEST_CASE("the plan of an imported image and buffer matches its snapshot")
    {
        ResourceDeclarations declarations;
        declarations.beginPass("Scene");
        declarations.importBuffer("Lights", Usage::ShaderRead, { .before = Usage::TransferDst });
        declarations.addTexture("SceneColor", Usage::ColorAttachment, color);
        declarations.beginPass("Present");
        declarations.addReference("SceneColor", Usage::TransferSrc);
        declarations.importTexture("Target", Usage::TransferDst,
            { .format = Format::BGRA8_SRGB, .before = Usage::ColorAttachment, .after = Usage::ColorAttachment });
        declarations.beginPass("UI");
        declarations.addReference("Target", Usage::ColorAttachment);
        declarations.beginPass("Capture");
        declarations.addReference("Target", Usage::TransferSrc);
        declarations.importBuffer("Pixels", Usage::TransferDst, { .after = Usage::TransferSrc });

        const auto plan = compilePlan(declarations);
        REQUIRE(plan.has_value());
        CHECK(describePlan(declarations, *plan) ==
              "1. Scene\n"
              "    import Lights ShaderRead\n"
              "    create SceneColor ColorAttachment\n"
              "    barrier Lights: TransferDst -> ShaderRead\n"
              "    barrier SceneColor: undefined -> ColorAttachment\n"
              "2. Present\n"
              "    use    SceneColor TransferSrc\n"
              "    import Target TransferDst\n"
              "    barrier SceneColor: ColorAttachment -> TransferSrc\n"
              "    barrier Target: ColorAttachment -> TransferDst\n"
              "3. UI\n"
              "    use    Target ColorAttachment\n"
              "    barrier Target: TransferDst -> ColorAttachment\n"
              "4. Capture\n"
              "    use    Target TransferSrc\n"
              "    import Pixels TransferDst\n"
              "    barrier Target: ColorAttachment -> TransferSrc\n"
              "end of frame\n"
              "    barrier Target: TransferSrc -> ColorAttachment\n"
              "    barrier Pixels: TransferDst -> TransferSrc\n"
              "resources\n"
              "    Lights: imported buffer\n"
              "    SceneColor: texture RGBA8_UNORM\n"
              "    Target: imported texture BGRA8_SRGB\n"
              "    Pixels: imported buffer\n");
    }
}
