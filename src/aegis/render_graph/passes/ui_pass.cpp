module;
// ImGui's Vulkan backend is a C API on the Vulkan headers; it is the one place the new stack hands
// raw handles to a library.
#include <imgui.h>
#include <imgui_impl_vulkan.h>

module aegis.render_graph;
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
}

UiPass::UiPass(ResourceDeclarations& declarations, Desc desc) :
    m_context{ desc.context },
    m_build{ std::move(desc.build) },
    m_final{ declarations.addReference(names::finalImage, Usage::ColorAttachment) }
{
}

UiPass::~UiPass()
{
    if (m_initialized)
        ImGui_ImplVulkan_Shutdown();
}

auto UiPass::info() const -> Info
{
    return { .name = "UI", .writes = { m_final } };
}

auto UiPass::createResources(ResourcePool& pool) -> std::expected<void, Error>
{
    auto& device = pool.device();
    const auto finalFormat = static_cast<VkFormat>(vk::Format::eR16G16B16A16Sfloat); // "Final", see PostProcessingPass

    ImGui_ImplVulkan_InitInfo initInfo{
        .ApiVersion = rhi::Context::vulkanVersion,
        .Instance = static_cast<VkInstance>(*m_context.instance()),
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
                .pColorAttachmentFormats = &finalFormat,
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
    } loader{ m_context.instance().getDispatcher()->vkGetInstanceProcAddr, initInfo.Instance };
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
    ImGui_ImplVulkan_NewFrame();
    m_build();
    ImGui::Render();

    const auto& cmd = frameInfo.cmd;
    std::array colorAttachments{ rhi::Attachment::colorLoad(pool.texture(m_final).view) };
    cmd.beginRendering({ .colorAttachments = colorAttachments });
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(cmd.handle()));
    cmd.endRendering();
}
}
