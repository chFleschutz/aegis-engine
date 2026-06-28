module;
#include <cassert>
#include <memory>
#include <utility>
#include <variant>

#include "vk_mem_alloc.h"

export module aegis.rhi:vulkan_conversions;
import :common;
import :commands;
import :utility;
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

constexpr auto toVulkan(Extent2D extent) noexcept -> vk::Extent2D;
constexpr auto toVulkan(Extent3D extent) noexcept -> vk::Extent3D;
constexpr auto toVulkan(Format format) noexcept -> vk::Format;
constexpr auto toVulkan(ShaderStage stage) noexcept -> vk::ShaderStageFlagBits;
constexpr auto toVulkan(ResourceState state) noexcept -> VulkanState;
constexpr auto toVulkan(ClearValue clearValue) noexcept -> vk::ClearValue;
constexpr auto toVulkan(AttachmentLoadOp op) noexcept -> vk::AttachmentLoadOp;
constexpr auto toVulkan(AttachmentStoreOp op) noexcept -> vk::AttachmentStoreOp;
constexpr auto toVulkan(BufferUsage usage) noexcept -> vk::BufferUsageFlags;
constexpr auto toVulkan(ImageUsage usage) noexcept -> vk::ImageUsageFlags;
constexpr auto toVulkan(DescriptorType type) noexcept -> vk::DescriptorType;

constexpr auto deriveImageAspectFlags(Format format) noexcept -> vk::ImageAspectFlags;
constexpr auto deriveAttachmentImageLayout(AttachmentStoreOp op) noexcept -> vk::ImageLayout;
constexpr auto deriveImageType(Extent3D extent) noexcept -> vk::ImageType;
constexpr auto deriveImageViewType(Extent3D extent, std::uint32_t arrayLayers) noexcept -> vk::ImageViewType;
constexpr auto deriveImageCreateFlags(Extent3D extent, std::uint32_t layers) noexcept -> vk::ImageCreateFlags;
constexpr auto deriveVmaInfo(MemoryUsage usage) noexcept -> VmaAllocationCreateInfo;

// Vulkan -> RHI conversion

constexpr auto toRHI(vk::Result result) noexcept -> ErrorCode;
constexpr auto toRHI(vk::Format format) noexcept -> Format;
constexpr auto toRHI(vk::Extent2D extent) noexcept -> Extent2D;

/////////////////////
// Implementations //
/////////////////////

constexpr auto toVulkan(Extent2D extent) noexcept -> vk::Extent2D
{
    return vk::Extent2D{ extent.x, extent.y };
}

constexpr auto toVulkan(Extent3D extent) noexcept -> vk::Extent3D
{
    return vk::Extent3D{ extent.x, extent.y, extent.z };
}

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
    case ResourceState::Attachment:
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

constexpr auto toVulkan(ClearValue clearValue) noexcept -> vk::ClearValue
{
    return utility::match(clearValue,
        [](const ClearColor& value) -> vk::ClearValue {
            return vk::ClearColorValue{ value.r, value.g, value.b, value.a };
        },
        [](const ClearDepthStencil& value)-> vk::ClearValue {
            return vk::ClearDepthStencilValue{ value.depth, value.stencil };
        }
    );
}

constexpr auto toVulkan(AttachmentLoadOp op) noexcept -> vk::AttachmentLoadOp
{
    switch (op)
    {
    case AttachmentLoadOp::Load:
        return vk::AttachmentLoadOp::eLoad;
    case AttachmentLoadOp::Clear:
        return vk::AttachmentLoadOp::eClear;
    case AttachmentLoadOp::DontCare:
        return vk::AttachmentLoadOp::eDontCare;
    }
    std::unreachable();
}

constexpr auto toVulkan(AttachmentStoreOp op) noexcept -> vk::AttachmentStoreOp
{
    switch (op)
    {
    case AttachmentStoreOp::Store:
        return vk::AttachmentStoreOp::eStore;
    case AttachmentStoreOp::DontCare:
        return vk::AttachmentStoreOp::eDontCare;
    case AttachmentStoreOp::None:
        return vk::AttachmentStoreOp::eNone;
    }
    std::unreachable();
}

constexpr auto toVulkan(BufferUsage usage) noexcept -> vk::BufferUsageFlags
{
    vk::BufferUsageFlags flags{};
    if (utility::hasFlag(usage, BufferUsage::Vertex))
        flags |= vk::BufferUsageFlagBits::eVertexBuffer;
    if (utility::hasFlag(usage, BufferUsage::Index))
        flags |= vk::BufferUsageFlagBits::eIndexBuffer;
    if (utility::hasFlag(usage, BufferUsage::Uniform))
        flags |= vk::BufferUsageFlagBits::eUniformBuffer;
    if (utility::hasFlag(usage, BufferUsage::Storage))
        flags |= vk::BufferUsageFlagBits::eStorageBuffer;
    if (utility::hasFlag(usage, BufferUsage::Indirect))
        flags |= vk::BufferUsageFlagBits::eIndirectBuffer;
    if (utility::hasFlag(usage, BufferUsage::TransferSrc))
        flags |= vk::BufferUsageFlagBits::eTransferSrc;
    if (utility::hasFlag(usage, BufferUsage::TransferDst))
        flags |= vk::BufferUsageFlagBits::eTransferDst;
    return flags;
}

constexpr auto toVulkan(ImageUsage usage) noexcept -> vk::ImageUsageFlags
{
    vk::ImageUsageFlags flags{};
    if (utility::hasFlag(usage, ImageUsage::Sampled))
        flags |= vk::ImageUsageFlagBits::eSampled;
    if (utility::hasFlag(usage, ImageUsage::Storage))
        flags |= vk::ImageUsageFlagBits::eStorage;
    if (utility::hasFlag(usage, ImageUsage::ColorAttachment))
        flags |= vk::ImageUsageFlagBits::eColorAttachment;
    if (utility::hasFlag(usage, ImageUsage::DepthStencilAttachment))
        flags |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
    if (utility::hasFlag(usage, ImageUsage::TransferSrc))
        flags |= vk::ImageUsageFlagBits::eTransferSrc;
    if (utility::hasFlag(usage, ImageUsage::TransferDst))
        flags |= vk::ImageUsageFlagBits::eTransferDst;
    return flags;
}

constexpr auto toVulkan(DescriptorType type) noexcept -> vk::DescriptorType
{
    switch (type)
    {
    case DescriptorType::SampledImage:
        return vk::DescriptorType::eSampledImage;
    case DescriptorType::StorageImage:
        return vk::DescriptorType::eStorageImage;
    case DescriptorType::Sampler:
        return vk::DescriptorType::eSampler;
    }
    std::unreachable();
}

constexpr auto deriveImageAspectFlags(Format format) noexcept -> vk::ImageAspectFlags
{
    switch (format)
    {
    case Format::Unknown:
        return vk::ImageAspectFlagBits::eNone;
    case Format::R8_UNORM:
    case Format::RG8_UNORM:
    case Format::RGBA8_UNORM:
    case Format::RGBA8_SRGB:
    case Format::BGRA8_UNORM:
    case Format::BGRA8_SRGB:
    case Format::RGB10A2_UNORM:
    case Format::B10G11R11_UFLOAT:
    case Format::R16_UNORM:
    case Format::RG16_UNORM:
    case Format::RGBA16_UNORM:
    case Format::RGBA16_SFLOAT:
    case Format::R32_SFLOAT:
    case Format::RG32_SFLOAT:
    case Format::RGB32_SFLOAT:
    case Format::RGBA32_SFLOAT:
        return vk::ImageAspectFlagBits::eColor;
    case Format::D32_SFLOAT:
        return vk::ImageAspectFlagBits::eDepth;
    case Format::D24_UNORM_S8_UINT:
    case Format::D32_SFLOAT_S8_UINT:
        return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
    }
    std::unreachable();
}

constexpr auto deriveAttachmentImageLayout(AttachmentStoreOp op) noexcept -> vk::ImageLayout
{
    if (op == AttachmentStoreOp::None)
        return vk::ImageLayout::eReadOnlyOptimal;
    return vk::ImageLayout::eAttachmentOptimal;
}

constexpr auto deriveImageType(Extent3D extent) noexcept -> vk::ImageType
{
    if (extent.z > 1)
        return vk::ImageType::e3D;
    if (extent.y > 1)
        return vk::ImageType::e2D;
    return vk::ImageType::e1D;
}

constexpr auto deriveImageViewType(Extent3D extent, std::uint32_t arrayLayers) noexcept -> vk::ImageViewType
{
    auto imageType = deriveImageType(extent);
    if (arrayLayers == 6 and imageType == vk::ImageType::e2D)
        return vk::ImageViewType::eCube;

    switch (imageType)
    {
    case vk::ImageType::e1D:
        return arrayLayers > 1 ? vk::ImageViewType::e1DArray : vk::ImageViewType::e1D;
    case vk::ImageType::e2D:
        return arrayLayers > 1 ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D;
    case vk::ImageType::e3D:
        return vk::ImageViewType::e3D;
    }
    std::unreachable();
}

constexpr auto deriveImageCreateFlags(Extent3D extent, std::uint32_t layers) noexcept -> vk::ImageCreateFlags
{
    vk::ImageCreateFlags flags{};

    // Cubemap: 6 array layers on 2D image
    if (layers == 6 and deriveImageType(extent) == vk::ImageType::e2D)
        flags |= vk::ImageCreateFlagBits::eCubeCompatible;

    return flags;
}

constexpr auto deriveVmaInfo(MemoryUsage usage) noexcept -> VmaAllocationCreateInfo
{
    switch (usage)
    {
    case MemoryUsage::GpuOnly:
        return VmaAllocationCreateInfo{
            .usage = VMA_MEMORY_USAGE_AUTO,
        };
    case MemoryUsage::CpuWrite:
        return VmaAllocationCreateInfo{
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                     VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
        };
    case MemoryUsage::CpuRead:
        return VmaAllocationCreateInfo{
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
            .preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
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

constexpr auto toRHI(vk::Extent2D extent) noexcept -> Extent2D
{
    return Extent2D{ extent.width, extent.height };
}
}
