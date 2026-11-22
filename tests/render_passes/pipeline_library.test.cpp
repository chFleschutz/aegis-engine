#include <doctest/doctest.h>

import std;
import aegis.render_passes;
import aegis.rhi;

namespace
{
using aegis::render_graph::ShaderEntry;
using aegis::render_graph::detail::computePipelineKey;
using aegis::render_graph::detail::graphicsPipelineKey;
using aegis::rhi::BlendMode;
using aegis::rhi::CullMode;
using aegis::rhi::Format;
using aegis::rhi::FrontFace;
using aegis::rhi::Pipeline;
using aegis::rhi::ShaderStage;

const std::array stages{
    ShaderEntry{ ShaderStage::Vertex, "sky_box.slang.spv", "vertexMain" },
    ShaderEntry{ ShaderStage::Fragment, "sky_box.slang.spv", "fragmentMain" },
};
const std::array colors{ Pipeline::ColorAttachment{ .format = Format::RGBA16_SFLOAT } };

auto skyBox() -> Pipeline::GraphicsDesc
{
    return { .name = "SkyBox", .colorAttachments = colors, .depthAttachment = Format::D32_SFLOAT, .cullMode = CullMode::Front };
}
}

TEST_SUITE("render_passes::PipelineLibrary")
{
    TEST_CASE("keys a compute pipeline by its shader and entry point")
    {
        CHECK(computePipelineKey("lighting.slang.spv", "main") == computePipelineKey("lighting.slang.spv", "main"));
        CHECK(computePipelineKey("lighting.slang.spv", "main") != computePipelineKey("post_process.slang.spv", "main"));
        CHECK(computePipelineKey("lighting.slang.spv", "main") != computePipelineKey("lighting.slang.spv", "other"));
    }

    TEST_CASE("keys a graphics pipeline the same whatever its name")
    {
        auto renamed = skyBox();
        renamed.name = "AnotherSkyBox";
        CHECK(graphicsPipelineKey(skyBox(), stages) == graphicsPipelineKey(renamed, stages));
    }

    TEST_CASE("keys graphics pipelines apart by what decides them")
    {
        const auto base = graphicsPipelineKey(skyBox(), stages);

        const std::array otherFormat{ Pipeline::ColorAttachment{ .format = Format::RGBA8_UNORM } };
        const std::array blended{ Pipeline::ColorAttachment{ .format = Format::RGBA16_SFLOAT, .blend = BlendMode::Alpha } };
        const std::array twoColors{ colors[0], colors[0] };
        std::vector<Pipeline::GraphicsDesc> changed(9, skyBox());
        changed[0].colorAttachments = otherFormat;
        changed[1].colorAttachments = blended;
        changed[2].colorAttachments = twoColors;
        changed[3].depthAttachment = Format::Unknown;
        changed[4].depth.write = false;
        changed[5].depth.compareOp = aegis::rhi::CompareOp::LessOrEqual;
        changed[6].cullMode = CullMode::Back;
        changed[7].frontFace = FrontFace::Clockwise;
        changed[8].stencilAttachment = Format::D32_SFLOAT;
        for (std::size_t i = 0; i < changed.size(); ++i)
        {
            CAPTURE(i);
            CHECK(graphicsPipelineKey(changed[i], stages) != base);
        }

        const std::array otherEntry{ stages[0], ShaderEntry{ ShaderStage::Fragment, "sky_box.slang.spv", "depthFragmentMain" } };
        const std::array otherFile{ stages[0], ShaderEntry{ ShaderStage::Fragment, "point_light.slang.spv", "fragmentMain" } };
        const std::array vertexOnly{ stages[0] };
        CHECK(graphicsPipelineKey(skyBox(), otherEntry) != base);
        CHECK(graphicsPipelineKey(skyBox(), otherFile) != base);
        CHECK(graphicsPipelineKey(skyBox(), vertexOnly) != base);
    }

    TEST_CASE("keys compute and graphics pipelines apart")
    {
        CHECK(computePipelineKey("sky_box.slang.spv", "vertexMain") != graphicsPipelineKey(skyBox(), stages));
    }

    TEST_CASE("starts empty without a device")
    {
        const aegis::render_graph::PipelineLibrary library;
        CHECK(library.size() == 0);
        CHECK(library.computedBrdfLut() == nullptr);
    }
}
