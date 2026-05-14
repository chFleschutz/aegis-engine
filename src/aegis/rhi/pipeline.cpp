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
auto Pipeline::create(const ComputeDesc& desc)
    -> std::expected<Pipeline, Error>
{
    const auto& device = desc.device.device();

    auto pipelineLayout = createPipelineLayout(device, desc.setLayouts, desc.pushConstantRanges);
    if (!pipelineLayout)
        return std::unexpected{ pipelineLayout.error() };

    auto pipeline = createComputePipeline(desc, *pipelineLayout);
    if (!pipeline)
        return std::unexpected{ pipeline.error() };

    return Pipeline{
        std::move(*pipeline),
        std::move(*pipelineLayout),
        vk::PipelineBindPoint::eCompute
    };
}

auto Pipeline::create(const GraphicsDesc& desc)
    -> std::expected<Pipeline, Error>
{
    const auto& device = desc.device.device();

    auto pipelineLayout = createPipelineLayout(device, desc.setLayouts, desc.pushConstantRanges);
    if (!pipelineLayout)
        return std::unexpected{ pipelineLayout.error() };

    auto pipeline = createGraphicsPipeline(desc, *pipelineLayout);
    if (!pipeline)
        return std::unexpected{ pipeline.error() };

    return Pipeline{
        std::move(*pipeline),
        std::move(*pipelineLayout),
        vk::PipelineBindPoint::eGraphics
    };
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

auto Pipeline::createComputePipeline(
    const ComputeDesc& desc,
    const vk::raii::PipelineLayout& layout)
    -> std::expected<vk::raii::Pipeline, Error>
{
    auto shaderModule = createShaderModule(desc.device.device(), desc.shader.code);
    if (!shaderModule)
        return std::unexpected{ shaderModule.error() };

    vk::ComputePipelineCreateInfo createInfo{
        .stage = {
            .stage = vk::ShaderStageFlagBits::eCompute,
            .module = *shaderModule,
            .pName = desc.shader.entryPoint.data(),
        },
        .layout = *layout,
        .basePipelineHandle = nullptr,
        .basePipelineIndex = -1,
    };

    auto [result, pipeline] = desc.device.device().createComputePipeline(nullptr, createInfo);
    if (result != vk::Result::eSuccess)
        return vkError(result, "Failed to create compute pipeline");

    return std::move(pipeline);
}

auto Pipeline::createGraphicsPipeline(
    const GraphicsDesc& desc,
    const vk::raii::PipelineLayout& pipelineLayout)
    -> std::expected<vk::raii::Pipeline, Error>
{
    std::vector<vk::raii::ShaderModule> shaderModules;
    shaderModules.reserve(desc.shaders.size());

    std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
    shaderStages.reserve(desc.shaders.size());

    for (const auto& [stage, code, entryPoint] : desc.shaders)
    {
        auto shaderModule = createShaderModule(desc.device.device(), code);
        if (!shaderModule)
            return std::unexpected{ shaderModule.error() };

        const auto& module = shaderModules.emplace_back(std::move(*shaderModule));
        shaderStages.emplace_back(
            vk::PipelineShaderStageCreateInfo{
                .stage = toVkType(stage),
                .module = *module,
                .pName = entryPoint.data(),
            });
    }

    auto vertexBindings = desc.vertexBindings
                          | std::views::transform([](const auto& b) {
                              return vk::VertexInputBindingDescription{
                                  .binding = b.binding,
                                  .stride = b.stride,
                                  .inputRate = vk::VertexInputRate::eVertex,
                              };
                          })
                          | std::ranges::to<std::vector>();

    auto vertexAttributes = desc.vertexAttributes
                            | std::views::transform([](const auto& a) {
                                return vk::VertexInputAttributeDescription{
                                    .location = a.location,
                                    .binding = a.binding,
                                    .format = toVk(a.format),
                                    .offset = a.offset,
                                };
                            })
                            | std::ranges::to<std::vector>();

    vk::PipelineVertexInputStateCreateInfo vertexInputState{
        .vertexBindingDescriptionCount = static_cast<std::uint32_t>(vertexBindings.size()),
        .pVertexBindingDescriptions = vertexBindings.data(),
        .vertexAttributeDescriptionCount = static_cast<std::uint32_t>(vertexAttributes.size()),
        .pVertexAttributeDescriptions = vertexAttributes.data(),
    };

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
    colorBlendAttachments.reserve(desc.colorAttachments.size());
    for (Format _ : desc.colorAttachments)
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

    auto formats = desc.colorAttachments | std::views::transform([](Format f) { return toVk(f); }) |
                   std::ranges::to<std::vector<vk::Format>>();

    auto structureChain = vk::StructureChain{
        vk::PipelineRenderingCreateInfo{
            .viewMask = 0,
            .colorAttachmentCount = static_cast<uint32_t>(formats.size()),
            .pColorAttachmentFormats = formats.data(),
            .depthAttachmentFormat = toVk(desc.depthAttachment),
            // .stencilAttachmentFormat =
        }
    };

    vk::GraphicsPipelineCreateInfo createInfo{
        .pNext = structureChain.get<vk::PipelineRenderingCreateInfo>(),
        .stageCount = static_cast<std::uint32_t>(shaderStages.size()),
        .pStages = shaderStages.data(),
        .pVertexInputState = &vertexInputState,
        .pInputAssemblyState = &inputAssemblyState,
        .pTessellationState = nullptr,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizationState,
        .pMultisampleState = &multisampleState,
        .pDepthStencilState = &depthStencilState,
        .pColorBlendState = &colorBlendState,
        .pDynamicState = &dynamicState,
        .layout = *pipelineLayout,
        .renderPass = nullptr,
        .subpass = 0,
        .basePipelineHandle = nullptr,
        .basePipelineIndex = -1,
    };

    auto [result, pipeline] = desc.device.device().createGraphicsPipeline(nullptr, createInfo);
    if (result != vk::Result::eSuccess)
        return vkError(result, "Failed to create graphics pipeline");

    return std::move(pipeline);
}

auto Pipeline::createShaderModule(const vk::raii::Device& device,
    std::span<std::uint32_t> code)
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
}
