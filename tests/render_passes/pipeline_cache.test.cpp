#include <doctest/doctest.h>

import std;
import aegis.assets;
import aegis.render_passes;
import aegis.rhi;

namespace
{
using aegis::assets::AlphaMode;
using aegis::assets::MaterialTemplate;
using aegis::assets::MaterialType;
using aegis::assets::MaterialVariant;
using aegis::render_graph::DrawBatch;
using aegis::render_graph::drawsBatch;
using aegis::render_graph::drawsForward;
using aegis::render_graph::drawsInGBuffer;
using aegis::render_graph::ErrorCode;
using aegis::render_graph::fragmentEntry;
using aegis::render_graph::GeometryPath;
using aegis::render_graph::PipelineCache;
using aegis::render_graph::PipelineKey;
using aegis::render_graph::PipelineState;
using aegis::render_graph::pipelineState;
using aegis::render_graph::Technique;
using aegis::render_graph::techniqueShader;
using aegis::rhi::BlendMode;
using aegis::rhi::CullMode;
using aegis::rhi::FrontFace;

auto makeKey() -> PipelineKey
{
    return { .variant = MaterialVariant{ .materialTemplate = std::make_shared<const MaterialTemplate>() } };
}
}

TEST_SUITE("render_passes::techniqueShader")
{
    TEST_CASE("names the technique's file built with the template's surface shader")
    {
        const MaterialVariant unlit{ .materialTemplate = std::make_shared<const MaterialTemplate>("unlit") };
        const MaterialVariant pbr{ .materialTemplate = std::make_shared<const MaterialTemplate>("pbr") };
        CHECK(techniqueShader("gpu_driven/geometry", unlit) == "gpu_driven/geometry.unlit.spv");
        CHECK(techniqueShader("cpu_driven/geometry", pbr) == "cpu_driven/geometry.pbr.spv");
    }

    TEST_CASE("names the masked build for a masked variant")
    {
        MaterialVariant variant{ .materialTemplate = std::make_shared<const MaterialTemplate>("pbr"), .alphaMode = AlphaMode::Mask };
        CHECK(techniqueShader("gpu_driven/geometry", variant) == "gpu_driven/geometry.pbr.masked.spv");
        variant.cullMode = CullMode::None;
        CHECK(techniqueShader("cpu_driven/geometry", variant) == "cpu_driven/geometry.pbr.masked.spv");
    }

    TEST_CASE("names the plain build for an opaque or blended variant")
    {
        const auto pbrTemplate = std::make_shared<const MaterialTemplate>("pbr");
        CHECK(techniqueShader("gpu_driven/geometry", MaterialVariant{ .materialTemplate = pbrTemplate, .cullMode = CullMode::None }) == "gpu_driven/geometry.pbr.spv");
        CHECK(techniqueShader("gpu_driven/geometry", MaterialVariant{ .materialTemplate = pbrTemplate, .alphaMode = AlphaMode::Blend }) == "gpu_driven/geometry.pbr.spv");
    }
}

TEST_SUITE("render_passes::pipelineState")
{
    TEST_CASE("the default variant culls back faces with counter-clockwise front faces and no blend")
    {
        CHECK(pipelineState(makeKey()) == PipelineState{ .cullMode = CullMode::Back, .frontFace = FrontFace::CounterClockwise, .blend = BlendMode::None });
    }

    TEST_CASE("a variant that culls nothing creates a pipeline that culls nothing")
    {
        auto key = makeKey();
        key.variant.cullMode = CullMode::None;
        CHECK(pipelineState(key).cullMode == CullMode::None);
    }

    TEST_CASE("the key's front face is the pipeline's, as the CPU path's mirrored instances need")
    {
        auto key = makeKey();
        key.frontFace = FrontFace::Clockwise;
        CHECK(pipelineState(key).frontFace == FrontFace::Clockwise);
        CHECK(pipelineState(key).cullMode == CullMode::Back);
    }

    TEST_CASE("the G-buffer blends nothing, even for a blended variant")
    {
        auto key = makeKey();
        key.variant.alphaMode = AlphaMode::Blend;
        CHECK(pipelineState(key).blend == BlendMode::None);
    }

    TEST_CASE("the depth technique blends nothing and keeps the variant's cull mode")
    {
        auto key = makeKey();
        key.technique = Technique::Depth;
        key.variant.alphaMode = AlphaMode::Blend;
        key.variant.cullMode = CullMode::None;
        CHECK(pipelineState(key) == PipelineState{ .cullMode = CullMode::None, .frontFace = FrontFace::CounterClockwise, .blend = BlendMode::None });
    }

    TEST_CASE("the forward technique blends with alpha, writes no depth and keeps the variant's cull mode and front face")
    {
        auto key = makeKey();
        key.technique = Technique::Forward;
        key.variant.alphaMode = AlphaMode::Blend;
        key.variant.cullMode = CullMode::None;
        key.frontFace = FrontFace::Clockwise;
        CHECK(pipelineState(key) == PipelineState{ .cullMode = CullMode::None, .frontFace = FrontFace::Clockwise, .blend = BlendMode::Alpha, .depthWrite = false });
    }

    TEST_CASE("the G-buffer and depth techniques write depth")
    {
        auto key = makeKey();
        CHECK(pipelineState(key).depthWrite);
        key.technique = Technique::Depth;
        CHECK(pipelineState(key).depthWrite);
    }
}

TEST_SUITE("render_passes::fragmentEntry")
{
    TEST_CASE("the G-buffer draws with fragmentMain, masked or not")
    {
        auto variant = makeKey().variant;
        CHECK(fragmentEntry(Technique::GBuffer, variant) == "fragmentMain");
        variant.alphaMode = AlphaMode::Mask;
        CHECK(fragmentEntry(Technique::GBuffer, variant) == "fragmentMain");
    }

    TEST_CASE("depth has a fragment stage only to alpha-test a masked variant")
    {
        auto variant = makeKey().variant;
        CHECK_FALSE(fragmentEntry(Technique::Depth, variant).has_value());
        variant.alphaMode = AlphaMode::Blend;
        CHECK_FALSE(fragmentEntry(Technique::Depth, variant).has_value());
        variant.alphaMode = AlphaMode::Mask;
        CHECK(fragmentEntry(Technique::Depth, variant) == "depthFragmentMain");
    }

    TEST_CASE("the forward technique draws with fragmentMain")
    {
        auto variant = makeKey().variant;
        variant.alphaMode = AlphaMode::Blend;
        CHECK(fragmentEntry(Technique::Forward, variant) == "fragmentMain");
    }
}

TEST_SUITE("render_passes::drawsBatch")
{
    auto makeBatch(AlphaMode alphaMode, std::uint32_t instanceCount = 1) -> DrawBatch
    {
        return { .id = 0, .firstInstance = 0, .instanceCount = instanceCount, .variant = { .materialTemplate = std::make_shared<const MaterialTemplate>(), .alphaMode = alphaMode } };
    }

    TEST_CASE("the forward technique draws the blended batches, the G-buffer and depth the others")
    {
        for (const auto mode : { AlphaMode::Opaque, AlphaMode::Mask })
        {
            CHECK(drawsInGBuffer(makeBatch(mode)));
            CHECK_FALSE(drawsForward(makeBatch(mode)));
            CHECK(drawsBatch(Technique::Depth, makeBatch(mode)));
            CHECK_FALSE(drawsBatch(Technique::Forward, makeBatch(mode)));
        }
        CHECK_FALSE(drawsInGBuffer(makeBatch(AlphaMode::Blend)));
        CHECK(drawsForward(makeBatch(AlphaMode::Blend)));
        CHECK(drawsBatch(Technique::Forward, makeBatch(AlphaMode::Blend)));
        CHECK_FALSE(drawsBatch(Technique::GBuffer, makeBatch(AlphaMode::Blend)));
        CHECK_FALSE(drawsBatch(Technique::Depth, makeBatch(AlphaMode::Blend)));
    }

    TEST_CASE("the forward technique draws a blended batch whatever its material type, but none without instances")
    {
        CHECK(drawsBatch(Technique::Forward, makeBatch(AlphaMode::Blend), MaterialType::Transparent));
        CHECK_FALSE(drawsForward(makeBatch(AlphaMode::Blend, 0)));
    }
}

TEST_SUITE("render_passes::PipelineKey")
{
    TEST_CASE("keys differ by each part and hash equal when equal")
    {
        const auto key = makeKey();
        auto mirrored = key;
        mirrored.frontFace = FrontFace::Clockwise;
        auto gpuDriven = key;
        gpuDriven.path = GeometryPath::GpuDriven;
        auto twoSided = key;
        twoSided.variant.cullMode = CullMode::None;
        auto depth = key;
        depth.technique = Technique::Depth;
        const auto otherTemplate = makeKey();

        CHECK(key != mirrored);
        CHECK(key != gpuDriven);
        CHECK(key != twoSided);
        CHECK(key != depth);
        CHECK(key != otherTemplate);
        auto copy = key;
        CHECK(copy == key);
        CHECK(PipelineKey::Hash{}(copy) == PipelineKey::Hash{}(key));
    }
}

TEST_SUITE("render_passes::PipelineCache")
{
    TEST_CASE("creates a pipeline the first time its key is prepared and keeps it")
    {
        int created{ 0 };
        PipelineCache<int> cache{ [&](const PipelineKey&) -> std::expected<int, aegis::render_graph::Error> { return ++created; } };
        const auto key = makeKey();
        auto mirrored = key;
        mirrored.frontFace = FrontFace::Clockwise;

        CHECK(cache.find(key) == nullptr);
        REQUIRE(cache.prepare(key));
        REQUIRE(cache.prepare(key));
        REQUIRE(cache.prepare(mirrored));
        REQUIRE(cache.prepare(key));

        CHECK(created == 2);
        CHECK(cache.size() == 2);
        REQUIRE(cache.find(key) != nullptr);
        CHECK(*cache.find(key) == 1);
        CHECK(*cache.find(mirrored) == 2);
    }

    TEST_CASE("a failed creation returns its error, caches nothing and is tried again")
    {
        int attempts{ 0 };
        PipelineCache<int> cache{ [&](const PipelineKey&) -> std::expected<int, aegis::render_graph::Error> {
            if (++attempts == 1)
                return aegis::render_graph::makeError(ErrorCode::Unsupported, "no shader");
            return 7;
        } };
        const auto key = makeKey();

        const auto failed = cache.prepare(key);
        REQUIRE(!failed.has_value());
        CHECK(failed.error().code == ErrorCode::Unsupported);
        CHECK(!cache.contains(key));

        REQUIRE(cache.prepare(key));
        CHECK(attempts == 2);
        CHECK(*cache.find(key) == 7);
    }
}
