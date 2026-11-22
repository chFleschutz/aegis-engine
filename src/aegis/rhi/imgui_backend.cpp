module;
// ImGui's Vulkan backend is a C API on the Vulkan headers; the rhi is where raw handles are handed to a library.
#include <cassert>
#include <imgui.h>
#include <imgui_impl_vulkan.h>

module aegis.rhi;
import std;
import :imgui_backend;
import :command_buffer;
import :context;
import :device;
import :image_view;
import :sampler;
import :vulkan_conversions;
import vulkan;

namespace aegis::rhi
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

auto ImGuiBackend::create(const Context& context, Device& device, const Desc& desc) -> std::expected<std::unique_ptr<ImGuiBackend>, Error>
{
    assert(ImGui::GetCurrentContext() && "The ImGui context must exist before its Vulkan backend");
    assert(!ImGui::GetIO().BackendRendererUserData && "The ImGui context already has a renderer backend");

    // No pipeline yet (no PipelineRenderingCreateInfo): the target's format comes with setTargetFormat.
    ImGui_ImplVulkan_InitInfo initInfo{
        .ApiVersion = Context::vulkanVersion,
        .Instance = static_cast<VkInstance>(*context.instance()),
        .PhysicalDevice = static_cast<VkPhysicalDevice>(*device.physicalDevice()),
        .Device = static_cast<VkDevice>(*device.device()),
        .QueueFamily = device.graphicsQueue().family(),
        .Queue = static_cast<VkQueue>(*device.graphicsQueue()),
        .DescriptorPoolSize = desc.descriptorPoolSize,
        .MinImageCount = 2,
        .ImageCount = std::max(desc.framesInFlight, 2u),
        .UseDynamicRendering = true,
    };
    // ImGui keeps its own function pointers (IMGUI_IMPL_VULKAN_NO_PROTOTYPES); load them through the
    // same instance the RHI dispatches through.
    struct Loader
    {
        PFN_vkGetInstanceProcAddr getInstanceProcAddr;
        VkInstance instance;
    } loader{ context.instance().getDispatcher()->vkGetInstanceProcAddr, initInfo.Instance };
    const auto loaded = ImGui_ImplVulkan_LoadFunctions(Context::vulkanVersion, [](const char* name, void* userData) {
            const auto& [getInstanceProcAddr, instance] = *static_cast<Loader*>(userData);
            const auto function = getInstanceProcAddr(instance, name);
            return function ? function : reinterpret_cast<PFN_vkVoidFunction>(&unavailable); }, &loader);
    if (!loaded || !ImGui_ImplVulkan_Init(&initInfo))
        return std::unexpected{ makeError(ErrorCode::InitializationFailed) };
    return std::unique_ptr<ImGuiBackend>{ new ImGuiBackend{} };
}

ImGuiBackend::~ImGuiBackend()
{
    ImGui_ImplVulkan_Shutdown();
}

auto ImGuiBackend::setTargetFormat(Format format) -> std::expected<void, Error>
{
    if (m_targetFormat == format)
        return {};
    switch (format)
    {
    case Format::RGBA8_UNORM:
    case Format::RGBA8_SRGB:
    case Format::BGRA8_UNORM:
    case Format::BGRA8_SRGB:
    case Format::RGB10A2_UNORM:
    case Format::RGBA16_SFLOAT:
    case Format::RGBA32_SFLOAT:
        break;
    default: return std::unexpected{ makeError(ErrorCode::FormatNotSupported) }; // Not a color format the UI draws onto
    }

    const auto vkFormat = static_cast<VkFormat>(toVulkan(format));
    ImGui_ImplVulkan_PipelineInfo pipelineInfo{
        .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
        .PipelineRenderingCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &vkFormat,
        },
    };
    ImGui_ImplVulkan_CreateMainPipeline(&pipelineInfo); // Copies the format
    m_targetFormat = format;
    return {};
}

auto ImGuiBackend::hasDrawData() const -> bool
{
    const auto* drawData = ImGui::GetDrawData();
    return drawData && drawData->Valid;
}

auto ImGuiBackend::record(const CommandBuffer& cmd) -> void
{
    assert(m_targetFormat && "setTargetFormat comes before record");
    auto* drawData = ImGui::GetDrawData();
    if (drawData && drawData->Valid)
        ImGui_ImplVulkan_RenderDrawData(drawData, static_cast<VkCommandBuffer>(cmd.handle()));
}

auto ImGuiBackend::addTexture(Device& device, SamplerHandle sampler, ImageViewHandle view) -> std::uint64_t
{
    // Read in the UI pass's fragment shader: every shader read state has this layout (ResourceState).
    const auto set = ImGui_ImplVulkan_AddTexture(static_cast<VkSampler>(device.get(sampler).vk()),
        static_cast<VkImageView>(device.get(view).vk()), static_cast<VkImageLayout>(vk::ImageLayout::eReadOnlyOptimal));
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(set));
}

auto ImGuiBackend::removeTexture(std::uint64_t texture) -> void
{
    ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(static_cast<std::uintptr_t>(texture)));
}
}
