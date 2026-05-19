module;
#include <cassert>
#include <utility>

export module aegis.rhi:vulkan;
import :common;
import vulkan_hpp;

export namespace aegis::rhi
{
struct VulkanState
{
    vk::ImageLayout layout;
    vk::PipelineStageFlags2 stageMask;
    vk::AccessFlags2 accessMask;
};

// RHI -> Vulkan conversion

constexpr auto toVulkan(Format format) noexcept -> vk::Format;
constexpr auto toVulkan(ShaderStage stage) noexcept -> vk::ShaderStageFlagBits;
constexpr auto toVulkan(ImageLayout layout) noexcept -> vk::ImageLayout;
constexpr auto toVulkan(ResourceState state) noexcept -> VulkanState;

// Vulkan -> RHI conversion

constexpr auto toRHI(vk::Result result) noexcept -> ErrorCode;
constexpr auto toRHI(vk::Format format) noexcept -> Format;

/////////////////////
// Implementations //
/////////////////////

constexpr auto toVulkan(Format format) noexcept -> vk::Format
{
    switch (format)
    {
    case Format::Unknown: return vk::Format::eUndefined;
    case Format::R8_UNORM: return vk::Format::eR8Unorm;
    case Format::RG8_UNORM: return vk::Format::eR8G8Unorm;
    case Format::RGBA8_UNORM: return vk::Format::eR8G8B8A8Unorm;
    case Format::RGBA8_SRGB: return vk::Format::eR8G8B8A8Srgb;
    case Format::BGRA8_UNORM: return vk::Format::eB8G8R8A8Unorm;
    case Format::BGRA8_SRGB: return vk::Format::eB8G8R8A8Srgb;
    case Format::RGB10A2_UNORM: return vk::Format::eA2R10G10B10UnormPack32;
    case Format::B10G11R11_UFLOAT: return vk::Format::eB10G11R11UfloatPack32;
    case Format::R16_UNORM: return vk::Format::eR16Unorm;
    case Format::RG16_UNORM: return vk::Format::eR16G16Unorm;
    case Format::RGBA16_UNORM: return vk::Format::eR16G16B16A16Unorm;
    case Format::RGBA16_SFLOAT: return vk::Format::eR16G16B16A16Sfloat;
    case Format::R32_SFLOAT: return vk::Format::eR32Sfloat;
    case Format::RG32_SFLOAT: return vk::Format::eR32G32Sfloat;
    case Format::RGB32_SFLOAT: return vk::Format::eR32G32B32Sfloat;
    case Format::RGBA32_SFLOAT: return vk::Format::eR32G32B32A32Sfloat;
    case Format::D32_SFLOAT: return vk::Format::eD32Sfloat;
    case Format::D24_UNORM_S8_UINT: return vk::Format::eD24UnormS8Uint;
    case Format::D32_SFLOAT_S8_UINT: return vk::Format::eD32SfloatS8Uint;
    }
    std::unreachable();
}

constexpr auto toVulkan(ShaderStage stage) noexcept -> vk::ShaderStageFlagBits
{
    switch (stage)
    {
    case ShaderStage::Vertex: return vk::ShaderStageFlagBits::eVertex;
    case ShaderStage::TessellationControl: return vk::ShaderStageFlagBits::eTessellationControl;
    case ShaderStage::TessellationEvaluation:
        return vk::ShaderStageFlagBits::eTessellationEvaluation;
    case ShaderStage::Geometry: return vk::ShaderStageFlagBits::eGeometry;
    case ShaderStage::Fragment: return vk::ShaderStageFlagBits::eFragment;
    case ShaderStage::Compute: return vk::ShaderStageFlagBits::eCompute;
    case ShaderStage::Task: return vk::ShaderStageFlagBits::eTaskEXT;
    case ShaderStage::Mesh: return vk::ShaderStageFlagBits::eMeshEXT;
    }
    std::unreachable();
}

constexpr auto toVulkan(ImageLayout layout) noexcept -> vk::ImageLayout
{
    switch (layout)
    {
    case ImageLayout::Unknown: return vk::ImageLayout::eUndefined;
    case ImageLayout::General: return vk::ImageLayout::eGeneral;
    case ImageLayout::Attachment: return vk::ImageLayout::eAttachmentOptimal;
    case ImageLayout::ReadOnly: return vk::ImageLayout::eReadOnlyOptimal;
    case ImageLayout::TransferSrc: return vk::ImageLayout::eTransferSrcOptimal;
    case ImageLayout::TransferDst: return vk::ImageLayout::eTransferDstOptimal;
    case ImageLayout::Present: return vk::ImageLayout::ePresentSrcKHR;
    }
    std::unreachable();
}

constexpr auto toVulkan(ResourceState state) noexcept -> VulkanState
{
    switch (state)
    {
    case ResourceState::Unknown:
        return VulkanState{
            .layout = vk::ImageLayout::eUndefined,
            .stageMask = vk::PipelineStageFlagBits2::eNone,
            .accessMask = vk::AccessFlagBits2::eNone,
        };
    case ResourceState::RenderTarget:
        return VulkanState{
            .layout = vk::ImageLayout::eAttachmentOptimal,
            .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .accessMask = vk::AccessFlagBits2::eColorAttachmentWrite |
                          vk::AccessFlagBits2::eColorAttachmentRead,
        };
    case ResourceState::DepthWrite:
        return VulkanState{
            .layout = vk::ImageLayout::eAttachmentOptimal,
            .stageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                         vk::PipelineStageFlagBits2::eLateFragmentTests,
            .accessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        };
    case ResourceState::DepthRead:
        return VulkanState{
            .layout = vk::ImageLayout::eReadOnlyOptimal,
            .stageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                         vk::PipelineStageFlagBits2::eLateFragmentTests,
            .accessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead,
        };
    case ResourceState::ShaderReadVertex:
        return VulkanState{
            .layout = vk::ImageLayout::eReadOnlyOptimal,
            .stageMask = vk::PipelineStageFlagBits2::eVertexShader,
            .accessMask = vk::AccessFlagBits2::eShaderRead,
        };
    case ResourceState::ShaderReadFragment:
        return VulkanState{
            .layout = vk::ImageLayout::eReadOnlyOptimal,
            .stageMask = vk::PipelineStageFlagBits2::eFragmentShader,
            .accessMask = vk::AccessFlagBits2::eShaderRead,
        };
    case ResourceState::ShaderReadCompute:
        return VulkanState{
            .layout = vk::ImageLayout::eReadOnlyOptimal,
            .stageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .accessMask = vk::AccessFlagBits2::eShaderRead,
        };
    case ResourceState::ComputeStorage:
        return VulkanState{
            .layout = vk::ImageLayout::eGeneral,
            .stageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .accessMask = vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite,
        };
    case ResourceState::CopySrc:
        return VulkanState{
            .layout = vk::ImageLayout::eTransferSrcOptimal,
            .stageMask = vk::PipelineStageFlagBits2::eCopy | vk::PipelineStageFlagBits2::eBlit,
            .accessMask = vk::AccessFlagBits2::eTransferRead
        };
    case ResourceState::CopyDst:
        return VulkanState{
            .layout = vk::ImageLayout::eTransferDstOptimal,
            .stageMask = vk::PipelineStageFlagBits2::eCopy | vk::PipelineStageFlagBits2::eBlit,
            .accessMask = vk::AccessFlagBits2::eTransferWrite
        };
    case ResourceState::Present:
        return VulkanState{
            .layout = vk::ImageLayout::ePresentSrcKHR,
            .stageMask = vk::PipelineStageFlagBits2::eNone,
            .accessMask = vk::AccessFlagBits2::eNone
        };
    }
    std::unreachable();
}

constexpr auto toRHI(vk::Result result) noexcept -> ErrorCode
{
    switch (result)
    {
    case vk::Result::eSuccess:
    {
        assert(false && "Success is not an error");
        return ErrorCode::Unknown;
    }
    case vk::Result::eErrorUnknown: return ErrorCode::Unknown;
    case vk::Result::eErrorDeviceLost: return ErrorCode::DeviceLost;
    case vk::Result::eErrorNativeWindowInUseKHR:
    case vk::Result::eErrorSurfaceLostKHR: return ErrorCode::SurfaceLost;
    case vk::Result::eSuboptimalKHR:
    case vk::Result::eErrorOutOfDateKHR: return ErrorCode::OutOfDate;
    case vk::Result::eErrorMemoryMapFailed:
    case vk::Result::eErrorOutOfHostMemory: return ErrorCode::OutOfHostMemory;
    case vk::Result::eErrorTooManyObjects:
    case vk::Result::eErrorOutOfDeviceMemory: return ErrorCode::OutOfDeviceMemory;
    case vk::Result::eErrorIncompatibleDriver:
    case vk::Result::eErrorLayerNotPresent:
    case vk::Result::eErrorExtensionNotPresent:
    case vk::Result::eErrorInitializationFailed: return ErrorCode::InitializationFailed;
    default: return ErrorCode::Unknown;
    }
}

constexpr auto toRHI(vk::Format format) noexcept -> Format
{
    switch (format)
    {
    case vk::Format::eUndefined: return Format::Unknown;
    case vk::Format::eR8Unorm: return Format::R8_UNORM;
    case vk::Format::eR8G8Unorm: return Format::RG8_UNORM;
    case vk::Format::eR8G8B8A8Unorm: return Format::RGBA8_UNORM;
    case vk::Format::eR8G8B8A8Srgb: return Format::RGBA8_SRGB;
    case vk::Format::eB8G8R8A8Unorm: return Format::BGRA8_UNORM;
    case vk::Format::eB8G8R8A8Srgb: return Format::BGRA8_SRGB;
    case vk::Format::eA2R10G10B10UnormPack32: return Format::RGB10A2_UNORM;
    case vk::Format::eB10G11R11UfloatPack32: return Format::B10G11R11_UFLOAT;
    case vk::Format::eR16Unorm: return Format::R16_UNORM;
    case vk::Format::eR16G16Unorm: return Format::RG16_UNORM;
    case vk::Format::eR16G16B16A16Unorm: return Format::RGBA16_UNORM;
    case vk::Format::eR16G16B16A16Sfloat: return Format::RGBA16_SFLOAT;
    case vk::Format::eR32Sfloat: return Format::R32_SFLOAT;
    case vk::Format::eR32G32Sfloat: return Format::RG32_SFLOAT;
    case vk::Format::eR32G32B32Sfloat: return Format::RGB32_SFLOAT;
    case vk::Format::eR32G32B32A32Sfloat: return Format::RGBA32_SFLOAT;
    case vk::Format::eD32Sfloat: return Format::D32_SFLOAT;
    case vk::Format::eD24UnormS8Uint: return Format::D24_UNORM_S8_UINT;
    case vk::Format::eD32SfloatS8Uint: return Format::D32_SFLOAT_S8_UINT;
    default:
    {
        assert(false && "Undefined format conversion");
        return Format::Unknown;
    }
    }
}
}
