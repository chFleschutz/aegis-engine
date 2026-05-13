module;
#include <array>
#include <expected>
#include <ranges>
#include <vector>

module aegis.rhi;
import :pipeline;
import :vulkan_common;

namespace aegis::rhi
{
auto Pipeline::create(const ComputeDesc& desc) -> std::expected<Pipeline, Error>
{
    const auto& device = desc.device.device();

    auto pipelineLayout = createPipelineLayout(device, desc.setLayouts, desc.pushConstantRanges);
    if (!pipelineLayout)
        return std::unexpected{ pipelineLayout.error() };

    auto shaderModule = createShaderModule(device, desc.shader.code);
    if (!shaderModule)
        return std::unexpected{ shaderModule.error() };

    auto pipeline =
        createComputePipeline(device, *pipelineLayout, *shaderModule, desc.shader.entryPoint);
    if (!pipeline)
        return std::unexpected{ pipeline.error() };

    return Pipeline{ std::move(*pipeline),
                     std::move(*pipelineLayout),
                     vk::PipelineBindPoint::eCompute };
}

auto Pipeline::create(const GraphicsDesc& desc) -> std::expected<Pipeline, Error>
{
    const auto& device = desc.device.device();

    auto pipelineLayout = createPipelineLayout(device, desc.setLayouts, desc.pushConstantRanges);
    if (!pipelineLayout)
        return std::unexpected{ pipelineLayout.error() };

    auto pipeline =
        createGraphicsPipeline(device, desc.shaders, desc.colorAttachments, desc.depthAttachment);
    if (!pipeline)
        return std::unexpected{ pipeline.error() };

    return Pipeline{ std::move(*pipeline),
                     std::move(*pipelineLayout),
                     vk::PipelineBindPoint::eGraphics };
}

Pipeline::Pipeline(
    vk::raii::Pipeline pipeline,
    vk::raii::PipelineLayout layout,
    vk::PipelineBindPoint bindPoint) :
    m_pipeline{ std::move(pipeline) },
    m_layout{ std::move(layout) },
    m_bindPoint{ bindPoint }
{
}

auto Pipeline::createPipelineLayout(
    const vk::raii::Device& device,
    std::span<vk::DescriptorSetLayout> setLayouts,
    std::span<vk::PushConstantRange> pushConstantRanges)
    -> std::expected<vk::raii::PipelineLayout, Error>
{
    vk::PipelineLayoutCreateInfo layoutCreateInfo{
        .setLayoutCount = static_cast<uint32_t>(setLayouts.size()),
        .pSetLayouts = setLayouts.data(),
        .pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size()),
        .pPushConstantRanges = pushConstantRanges.data(),
    };
    auto [result, pipelineLayout] = device.createPipelineLayout(layoutCreateInfo);
    if (result != vk::Result::eSuccess)
        return vkError(result, "Failed to create pipeline layout");

    return std::move(pipelineLayout);
}

auto Pipeline::createShaderModule(const vk::raii::Device& device, std::span<std::uint32_t> code)
    -> std::expected<vk::raii::ShaderModule, Error>
{
    vk::ShaderModuleCreateInfo shaderModuleCreateInfo{
        .codeSize = code.size() * sizeof(std::uint32_t),
        .pCode = code.data(),
    };

    auto [result, module] = device.createShaderModule(shaderModuleCreateInfo);
    if (result != vk::Result::eSuccess)
        return vkError(result, "Failed to create shader module");

    return std::move(module);
}

auto Pipeline::createComputePipeline(
    const vk::raii::Device& device,
    const vk::raii::PipelineLayout& layout,
    const vk::raii::ShaderModule& module,
    std::string_view entryPoint) -> std::expected<vk::raii::Pipeline, Error>
{
    vk::ComputePipelineCreateInfo createInfo{
        .stage = {
            .stage = vk::ShaderStageFlagBits::eCompute,
            .module = *module,
            .pName = entryPoint.data(),
        },
        .layout = *layout,
        .basePipelineHandle = nullptr,
        .basePipelineIndex = -1,
    };

    auto [result, pipeline] = device.createComputePipeline(nullptr, createInfo);
    if (result != vk::Result::eSuccess)
        return vkError(result, "Failed to create compute pipeline");

    return std::move(pipeline);
}

auto Pipeline::createGraphicsPipeline(
    const vk::raii::Device& device,
    std::span<Shader> shaders,
    std::span<Format> colorAttachments,
    Format depthAttachment) -> std::expected<vk::raii::Pipeline, Error>
{
    std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
    std::vector<vk::ShaderModule> modules;
    for (const auto& [type, code, entryPoint] : shaders)
    {
        auto module = createShaderModule(device, code);
        if (!module)
            return std::unexpected{ module.error() };

        vk::PipelineShaderStageCreateInfo s{
            .stage = toVkType(type),
            .module = *module,
            .pName = entryPoint.data(),
        };
        shaderStages.emplace_back(s);
        modules.emplace_back(std::move(*module));
    }

    vk::VertexInputBindingDescription bindingDescription{
        .binding = 0,
        .stride = 44, // TODO: use sizeof of redesign this
        .inputRate = vk::VertexInputRate::eVertex,
    };

    auto vertexAttributes = std::array{
        vk::VertexInputAttributeDescription{
            .location = 0,
            .binding = 0,
            .format = vk::Format::eR32G32B32Sfloat,
            .offset = 0,
        },
        vk::VertexInputAttributeDescription{
            .location = 1,
            .binding = 0,
            .format = vk::Format::eR32G32B32Sfloat,
            .offset = 12,
        },
        vk::VertexInputAttributeDescription{
            .location = 2,
            .binding = 0,
            .format = vk::Format::eR32G32Sfloat,
            .offset = 24,
        },
        vk::VertexInputAttributeDescription{
            .location = 3,
            .binding = 0,
            .format = vk::Format::eR32G32B32Sfloat,
            .offset = 32,
        },
    };

    vk::PipelineVertexInputStateCreateInfo vertexInputState{};
    vertexInputState.setPVertexBindingDescriptions(&bindingDescription);
    vertexInputState.setVertexAttributeDescriptions(vertexAttributes);

    vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState{
        .topology = vk::PrimitiveTopology::eTriangleList,
        .primitiveRestartEnable = vk::False,
    };

    vk::PipelineViewportStateCreateInfo viewportState{
        .viewportCount = 1,
        .scissorCount = 1,
    };

    vk::PipelineRasterizationStateCreateInfo rasterizationState{
        .depthClampEnable = vk::False,
        .rasterizerDiscardEnable = vk::False,
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eBack,
        .frontFace = vk::FrontFace::eCounterClockwise,
        .depthBiasEnable = vk::False,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp = 0.0f,
        .depthBiasSlopeFactor = 0.0f,
        .lineWidth = 1.0f,
    };

    vk::PipelineMultisampleStateCreateInfo multisampleState{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable = vk::False,
        .minSampleShading = 1.0f,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = vk::False,
        .alphaToOneEnable = vk::False,
    };

    vk::PipelineDepthStencilStateCreateInfo depthStencilState{
        .depthTestEnable = vk::True,
        .depthWriteEnable = vk::True,
        .depthCompareOp = vk::CompareOp::eLess,
        .depthBoundsTestEnable = vk::False,
        .stencilTestEnable = vk::False,
        .front = {},
        .back = {},
        .minDepthBounds = 0.0f,
        .maxDepthBounds = 1.0f,
    };

    std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments{};
    colorBlendAttachments.reserve(colorAttachments.size());
    for (Format _ : colorAttachments)
    {
        vk::PipelineColorBlendAttachmentState colorBlend{
            .blendEnable = vk::False,
            .srcColorBlendFactor = vk::BlendFactor::eOne,
            .dstColorBlendFactor = vk::BlendFactor::eZero,
            .colorBlendOp = vk::BlendOp::eAdd,
            .srcAlphaBlendFactor = vk::BlendFactor::eOne,
            .dstAlphaBlendFactor = vk::BlendFactor::eZero,
            .alphaBlendOp = vk::BlendOp::eAdd,
            .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
        };
        colorBlendAttachments.emplace_back(colorBlend);
    }

    vk::PipelineColorBlendStateCreateInfo colorBlendState{
        .logicOpEnable = vk::False,
        .logicOp = vk::LogicOp::eCopy,
        .attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size()),
        .pAttachments = colorBlendAttachments.data(),
        .blendConstants = std::array{ 0.0f, 0.0f, 0.0f, 0.0f },
    };

    auto dynamicStates = std::array{ vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.setDynamicStates(dynamicStates);

    auto formats = colorAttachments | std::views::transform([](Format f) { return toVk(f); }) |
        std::ranges::to<std::vector<vk::Format>>();

    auto structureChain = vk::StructureChain{ vk::PipelineRenderingCreateInfo{
        .viewMask = 0,
        .colorAttachmentCount = static_cast<uint32_t>(formats.size()),
        .pColorAttachmentFormats = formats.data(),
        .depthAttachmentFormat = toVk(depthAttachment),
        // .stencilAttachmentFormat =
    } };

    vk::GraphicsPipelineCreateInfo createInfo{
        .pNext = structureChain.get<vk::PipelineRenderingCreateInfo>(),
        .pVertexInputState = &vertexInputState,
        .pInputAssemblyState = &inputAssemblyState,
        .pTessellationState = nullptr,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizationState,
        .pMultisampleState = &multisampleState,
        .pDepthStencilState = &depthStencilState,
        .pColorBlendState = &colorBlendState,
        .pDynamicState = &dynamicState,
        .layout = nullptr,
        .renderPass = nullptr,
        .subpass = 0,
        .basePipelineHandle = nullptr,
        .basePipelineIndex = -1,
    };
    createInfo.setStages(shaderStages);

    auto [result, pipeline] = device.createGraphicsPipeline(nullptr, createInfo);
    if (result != vk::Result::eSuccess)
        return vkError(result, "Failed to create graphics pipeline");

    return std::move(pipeline);
}
}
