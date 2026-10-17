module;
#include <cassert>

#include "vk_mem_alloc.h"

export module aegis.rhi:vulkan_conversions;
import std;
import :common;
import :commands;
import :utility;
import vulkan;

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
auto toVulkan(ClearValue clearValue) noexcept -> vk::ClearValue; // Not constexpr: MSVC rejects building the union in one
constexpr auto toVulkan(AttachmentLoadOp op) noexcept -> vk::AttachmentLoadOp;
constexpr auto toVulkan(AttachmentStoreOp op) noexcept -> vk::AttachmentStoreOp;
constexpr auto toVulkan(BufferUsage usage) noexcept -> vk::BufferUsageFlags;
constexpr auto toVulkan(ImageUsage usage) noexcept -> vk::ImageUsageFlags;
constexpr auto toVulkan(DescriptorType type) noexcept -> vk::DescriptorType;
constexpr auto toVulkan(Filter filter) noexcept -> vk::Filter;
constexpr auto toVulkan(SamplerMipmapMode mode) noexcept -> vk::SamplerMipmapMode;
constexpr auto toVulkan(SamplerAddressMode mode) noexcept -> vk::SamplerAddressMode;
constexpr auto toVulkan(TimestampStage stage) noexcept -> vk::PipelineStageFlags2;
constexpr auto toVulkan(CompareOp op) noexcept -> vk::CompareOp;
constexpr auto toVulkan(CullMode mode) noexcept -> vk::CullModeFlags;
constexpr auto toVulkan(FrontFace face) noexcept -> vk::FrontFace;
constexpr auto toVulkan(BlendMode mode) noexcept -> vk::PipelineColorBlendAttachmentState;

constexpr auto deriveImageAspectFlags(Format format) noexcept -> vk::ImageAspectFlags;
constexpr auto deriveAttachmentImageLayout(AttachmentStoreOp op) noexcept -> vk::ImageLayout;
constexpr auto toVulkan(ImageType type) noexcept -> vk::ImageType;
constexpr auto toVulkan(ImageViewType type) noexcept -> vk::ImageViewType;
constexpr auto deriveImageCreateFlags(ImageType type) noexcept -> vk::ImageCreateFlags;
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

constexpr auto toVulkan(CompareOp op) noexcept -> vk::CompareOp
{
    switch (op)
    {
    case CompareOp::Never: return vk::CompareOp::eNever;
    case CompareOp::Less: return vk::CompareOp::eLess;
    case CompareOp::Equal: return vk::CompareOp::eEqual;
    case CompareOp::LessOrEqual: return vk::CompareOp::eLessOrEqual;
    case CompareOp::Greater: return vk::CompareOp::eGreater;
    case CompareOp::NotEqual: return vk::CompareOp::eNotEqual;
    case CompareOp::GreaterOrEqual: return vk::CompareOp::eGreaterOrEqual;
    case CompareOp::Always: return vk::CompareOp::eAlways;
    }
    std::unreachable();
}

constexpr auto toVulkan(CullMode mode) noexcept -> vk::CullModeFlags
{
    switch (mode)
    {
    case CullMode::None: return vk::CullModeFlagBits::eNone;
    case CullMode::Front: return vk::CullModeFlagBits::eFront;
    case CullMode::Back: return vk::CullModeFlagBits::eBack;
    case CullMode::FrontAndBack: return vk::CullModeFlagBits::eFrontAndBack;
    }
    std::unreachable();
}

constexpr auto toVulkan(FrontFace face) noexcept -> vk::FrontFace
{
    switch (face)
    {
    case FrontFace::CounterClockwise: return vk::FrontFace::eCounterClockwise;
    case FrontFace::Clockwise: return vk::FrontFace::eClockwise;
    }
    std::unreachable();
}

constexpr auto toVulkan(BlendMode mode) noexcept -> vk::PipelineColorBlendAttachmentState
{
    vk::PipelineColorBlendAttachmentState state{
        .blendEnable = vk::False,
        .srcColorBlendFactor = vk::BlendFactor::eOne,
        .dstColorBlendFactor = vk::BlendFactor::eZero,
        .colorBlendOp = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor = vk::BlendFactor::eOne,
        .dstAlphaBlendFactor = vk::BlendFactor::eZero,
        .alphaBlendOp = vk::BlendOp::eAdd,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };

    switch (mode)
    {
    case BlendMode::None:
        break;
    case BlendMode::Alpha:
        state.blendEnable = vk::True;
        state.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        state.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        state.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        break;
    case BlendMode::Additive:
        state.blendEnable = vk::True;
        state.dstColorBlendFactor = vk::BlendFactor::eOne;
        state.dstAlphaBlendFactor = vk::BlendFactor::eOne;
        break;
    }
    return state;
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
        // All stages rather than none: the transition must wait for whatever used the resource
        // before, e.g. a swapchain image's acquire semaphore or the previous frame's depth writes.
        return VulkanState{
            .layout = vk::ImageLayout::eUndefined,
            .stageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .accessMask = vk::AccessFlagBits2::eNone,
        };
    case ResourceState::ColorAttachment:
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
    case ResourceState::ShaderRead:
        return VulkanState{
            .layout = vk::ImageLayout::eReadOnlyOptimal,
            .stageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .accessMask = vk::AccessFlagBits2::eShaderRead,
        };
    case ResourceState::IndirectRead:
        return VulkanState{
            .layout = vk::ImageLayout::eUndefined, // Never an image state
            .stageMask = vk::PipelineStageFlagBits2::eDrawIndirect,
            .accessMask = vk::AccessFlagBits2::eIndirectCommandRead,
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
        // All transfer stages: fills and clears write from the clear stage, not copy or blit.
        return VulkanState{
            .layout = vk::ImageLayout::eTransferDstOptimal,
            .stageMask = vk::PipelineStageFlagBits2::eAllTransfer,
            .accessMask = vk::AccessFlagBits2::eTransferWrite
        };
    case ResourceState::Present:
        // All stages rather than none: the render-finished semaphore signal must cover the
        // transition, and it only covers work in its stage mask.
        return VulkanState{
            .layout = vk::ImageLayout::ePresentSrcKHR,
            .stageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .accessMask = vk::AccessFlagBits2::eNone
        };
    }
    std::unreachable();
}

inline auto toVulkan(ClearValue clearValue) noexcept -> vk::ClearValue
{
    return utility::match(
        clearValue,
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
    if (utility::hasFlag(usage, BufferUsage::ShaderDeviceAddress))
        flags |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
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

constexpr auto toVulkan(Filter filter) noexcept -> vk::Filter
{
    switch (filter)
    {
    case Filter::Nearest:
        return vk::Filter::eNearest;
    case Filter::Linear:
        return vk::Filter::eLinear;
    }
    std::unreachable();
}

constexpr auto toVulkan(SamplerMipmapMode mode) noexcept -> vk::SamplerMipmapMode
{
    switch (mode)
    {
    case SamplerMipmapMode::Nearest:
        return vk::SamplerMipmapMode::eNearest;
    case SamplerMipmapMode::Linear:
        return vk::SamplerMipmapMode::eLinear;
    }
    std::unreachable();
}

constexpr auto toVulkan(SamplerAddressMode mode) noexcept -> vk::SamplerAddressMode
{
    switch (mode)
    {
    case SamplerAddressMode::Repeat:
        return vk::SamplerAddressMode::eRepeat;
    case SamplerAddressMode::MirroredRepeat:
        return vk::SamplerAddressMode::eMirroredRepeat;
    case SamplerAddressMode::ClampToEdge:
        return vk::SamplerAddressMode::eClampToEdge;
    case SamplerAddressMode::ClampToBorder:
        return vk::SamplerAddressMode::eClampToBorder;
    }
    std::unreachable();
}

constexpr auto toVulkan(TimestampStage stage) noexcept -> vk::PipelineStageFlags2
{
    switch (stage)
    {
    case TimestampStage::TopOfPipe:
        return vk::PipelineStageFlagBits2::eTopOfPipe;
    case TimestampStage::AllCommands:
        return vk::PipelineStageFlagBits2::eAllCommands;
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

constexpr auto toVulkan(ImageType type) noexcept -> vk::ImageType
{
    switch (type)
    {
    case ImageType::Dim1D:
        return vk::ImageType::e1D;
    case ImageType::Dim2D:
    case ImageType::Cube:
        return vk::ImageType::e2D;
    case ImageType::Dim3D:
        return vk::ImageType::e3D;
    }
    std::unreachable();
}

constexpr auto toVulkan(ImageViewType type) noexcept -> vk::ImageViewType
{
    switch (type)
    {
    case ImageViewType::Dim1D:
        return vk::ImageViewType::e1D;
    case ImageViewType::Dim2D:
        return vk::ImageViewType::e2D;
    case ImageViewType::Dim3D:
        return vk::ImageViewType::e3D;
    case ImageViewType::Cube:
        return vk::ImageViewType::eCube;
    case ImageViewType::Dim1DArray:
        return vk::ImageViewType::e1DArray;
    case ImageViewType::Dim2DArray:
        return vk::ImageViewType::e2DArray;
    case ImageViewType::CubeArray:
        return vk::ImageViewType::eCubeArray;
    }
    std::unreachable();
}

constexpr auto deriveImageCreateFlags(ImageType type) noexcept -> vk::ImageCreateFlags
{
    return type == ImageType::Cube ? vk::ImageCreateFlagBits::eCubeCompatible : vk::ImageCreateFlags{};
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
    case vk::Result::eNotReady: return ErrorCode::NotReady;
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
