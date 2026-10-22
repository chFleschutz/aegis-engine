module;
// ImGui's Vulkan backend is a C API on the Vulkan headers; it is the one place the new stack hands
// raw handles to a library.
#include <imgui.h>
#include <imgui_impl_vulkan.h>

module aegis.render_passes;
import std;
import aegis.renderer;
import aegis.rhi;
import vulkan;

namespace aegis::render_graph
{
namespace
{
// Stands in for functions the instance lacks: ImGui loads surface and swapchain functions for its own
// window helpers, which a headless instance does not have and the renderer backend never calls.
auto unavailable() -> void
{
    std::abort();
}

// The formats a color attachment of the UI can have; ImGui's backend takes the Vulkan enum.
auto toVkFormat(rhi::Format format) -> std::optional<VkFormat>
{
    switch (format)
    {
    case rhi::Format::RGBA8_UNORM: return static_cast<VkFormat>(vk::Format::eR8G8B8A8Unorm);
    case rhi::Format::RGBA8_SRGB: return static_cast<VkFormat>(vk::Format::eR8G8B8A8Srgb);
    case rhi::Format::BGRA8_UNORM: return static_cast<VkFormat>(vk::Format::eB8G8R8A8Unorm);
    case rhi::Format::BGRA8_SRGB: return static_cast<VkFormat>(vk::Format::eB8G8R8A8Srgb);
    case rhi::Format::RGB10A2_UNORM: return static_cast<VkFormat>(vk::Format::eA2B10G10R10UnormPack32);
    case rhi::Format::RGBA16_SFLOAT: return static_cast<VkFormat>(vk::Format::eR16G16B16A16Sfloat);
    case rhi::Format::RGBA32_SFLOAT: return static_cast<VkFormat>(vk::Format::eR32G32B32A32Sfloat);
    default: return std::nullopt;
    }
}
}

UiPass::UiPass(ResourceDeclarations& declarations, Desc desc) :
    m_context{ desc.context },
    m_targetFormat{ desc.targetFormat },
    // Onto the frame's target the read only orders the scene passes first and readies "Final" to be shown.
    m_final{ declarations.addReference(names::finalImage, desc.targetFormat ? Usage::SampledFragment : Usage::ColorAttachment) }
{
}

UiPass::~UiPass()
{
    if (m_initialized)
        ImGui_ImplVulkan_Shutdown();
}

auto UiPass::name() const -> std::string_view
{
    return "UI";
}

auto UiPass::createResources(ResourcePool& pool) -> std::expected<void, Error>
{
    if (!m_context)
        return makeError(ErrorCode::InitializationFailed, "The UI pass needs the rhi::Context");
    const auto targetFormat = m_targetFormat.value_or(pool.format(m_final));
    const auto vkTargetFormat = toVkFormat(targetFormat);
    if (!vkTargetFormat)
        return makeError(ErrorCode::InitializationFailed, std::format("The UI pass cannot draw onto a {} target", rhi::formatName(targetFormat)));

    auto& device = pool.device();

    ImGui_ImplVulkan_InitInfo initInfo{
        .ApiVersion = rhi::Context::vulkanVersion,
        .Instance = static_cast<VkInstance>(*m_context->instance()),
        .PhysicalDevice = static_cast<VkPhysicalDevice>(*device.physicalDevice()),
        .Device = static_cast<VkDevice>(*device.device()),
        .QueueFamily = device.graphicsQueue().family(),
        .Queue = static_cast<VkQueue>(*device.graphicsQueue()),
        .DescriptorPoolSize = descriptorPoolSize,
        .MinImageCount = 2,
        // One set of ImGui's vertex and index buffers per frame in flight; it cycles them on its own counter.
        .ImageCount = renderer::Renderer::maxFramesInFlight,
        .PipelineInfoMain = {
            .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
            .PipelineRenderingCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                .colorAttachmentCount = 1,
                .pColorAttachmentFormats = &*vkTargetFormat,
            },
        },
        .UseDynamicRendering = true,
    };
    // ImGui keeps its own function pointers (IMGUI_IMPL_VULKAN_NO_PROTOTYPES); load them through the
    // same instance the RHI dispatches through.
    struct Loader
    {
        PFN_vkGetInstanceProcAddr getInstanceProcAddr;
        VkInstance instance;
    } loader{ m_context->instance().getDispatcher()->vkGetInstanceProcAddr, initInfo.Instance };
    const auto loaded = ImGui_ImplVulkan_LoadFunctions(rhi::Context::vulkanVersion, [](const char* name, void* userData) {
            const auto& [getInstanceProcAddr, instance] = *static_cast<Loader*>(userData);
            const auto function = getInstanceProcAddr(instance, name);
            return function ? function : reinterpret_cast<PFN_vkVoidFunction>(&unavailable); }, &loader);
    if (!loaded || !ImGui_ImplVulkan_Init(&initInfo))
        return makeError(ErrorCode::InitializationFailed, "Failed to initialize ImGui's Vulkan backend");
    m_initialized = true;
    return {};
}

auto UiPass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    // The caller ended the frame (ImGui::Render); without one there is nothing to draw. An empty frame
    // still goes through the backend, which uploads the font atlas and other textures with it.
    // The frame's target is cleared even then: nothing else draws onto it.
    auto* drawData = ImGui::GetDrawData();
    const bool draws = drawData && drawData->Valid;
    if (!draws && !m_targetFormat)
        return;

    const auto& cmd = frameInfo.cmd;
    std::array colorAttachments{ m_targetFormat
                                     ? rhi::Attachment::color(frameInfo.target, rhi::ClearColor{ 0.0f, 0.0f, 0.0f, 1.0f })
                                     : rhi::Attachment::colorLoad(pool.texture(m_final).view) };
    cmd.beginRendering({ .colorAttachments = colorAttachments });
    if (draws)
        ImGui_ImplVulkan_RenderDrawData(drawData, static_cast<VkCommandBuffer>(cmd.handle()));
    cmd.endRendering();
}
}
