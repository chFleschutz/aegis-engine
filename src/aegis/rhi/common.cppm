module;
#include <cstdint>
#include <utility>
#include <variant>

export module aegis.rhi:common;
import :utility;

export namespace aegis::rhi
{
using TimelineValue = std::uint64_t;

enum class ErrorCode
{
    Unknown,
    DeviceLost,
    SurfaceLost,
    OutOfDate,
    OutOfHostMemory,
    OutOfDeviceMemory,
    InitializationFailed,
};

enum class Format
{
    Unknown,
    // 8-bit
    R8_UNORM,
    RG8_UNORM,
    RGBA8_UNORM,
    RGBA8_SRGB,
    BGRA8_UNORM,
    BGRA8_SRGB,
    // 10/11-bit packed
    RGB10A2_UNORM,
    B10G11R11_UFLOAT,
    // 16-bit
    R16_UNORM,
    RG16_UNORM,
    RGBA16_UNORM,
    RGBA16_SFLOAT,
    // 32-bit
    R32_SFLOAT,
    RG32_SFLOAT,
    RGB32_SFLOAT,
    RGBA32_SFLOAT,
    // Depth/Stencil
    D32_SFLOAT,
    D24_UNORM_S8_UINT,
    D32_SFLOAT_S8_UINT,
};

enum class ShaderStage
{
    Vertex,
    TessellationControl,
    TessellationEvaluation,
    Geometry,
    Fragment,
    Compute,
    Task,
    Mesh,
};

enum class ResourceState
{
    Unknown,
    Attachment,
    DepthWrite,
    DepthRead,
    ShaderReadVertex,
    ShaderReadFragment,
    ShaderReadCompute,
    ComputeStorage,
    CopySrc,
    CopyDst,
    Present,
};

enum class AttachmentLoadOp
{
    Load,
    Clear,
    DontCare,
};

enum class AttachmentStoreOp
{
    Store,
    DontCare,
    None,
};

enum class BufferUsage : std::uint32_t
{
    None        = 0,
    Vertex      = 1 << 0,
    Index       = 1 << 1,
    Uniform     = 1 << 2,
    Storage     = 1 << 3,
    Indirect    = 1 << 4,
    TransferSrc = 1 << 5,
    TransferDst = 1 << 6,
    CpuVisible  = 1 << 7,
};

template<>
constexpr auto utility::isFlagEnum<BufferUsage>{ true };

enum class ImageUsage : std::uint32_t
{
    None                   = 0,
    Sampled                = 1 << 0,
    Storage                = 1 << 1,
    ColorAttachment        = 1 << 2,
    DepthStencilAttachment = 1 << 3,
    TransferSrc            = 1 << 4,
    TransferDst            = 1 << 5,
};

template<>
constexpr auto utility::isFlagEnum<ImageUsage>{ true };

enum class MemoryUsage
{
    GpuOnly,  // Device local, no CPU access
    CpuWrite, // Persistent map, sequential write (staging, uniforms)
    CpuRead,  // Persistent map, random read + cached (readback)
};

/// @brief Describes all descriptor types.
/// @note Buffers are not accessed via descriptors and use buffer device address instead.
enum class DescriptorType
{
    SampledImage,
    StorageImage,
    Sampler
};

struct Extent2D
{
    std::uint32_t x{ 0 };
    std::uint32_t y{ 0 };

    Extent2D(std::uint32_t x, std::uint32_t y) :
        x{ x },
        y{ y }
    {
    }

    explicit Extent2D(std::pair<std::uint32_t, std::uint32_t> pair) :
        x{ pair.first },
        y{ pair.second }
    {
    }
};

struct Extent3D
{
    std::uint32_t x;
    std::uint32_t y;
    std::uint32_t z;

    Extent3D(std::uint32_t x, std::uint32_t y, std::uint32_t z) :
        x{ x },
        y{ y },
        z{ z }
    {
    }

    explicit Extent3D(Extent2D extent) :
        x{ extent.x },
        y{ extent.y },
        z{ 1 }
    {
    }

    [[nodiscard]] auto toExtent2D() const noexcept -> Extent2D { return { x, y }; }
};

struct ClearColor
{
    float r{ 0.0f };
    float g{ 0.0f };
    float b{ 0.0f };
    float a{ 0.0f };
};

struct ClearDepthStencil
{
    float depth{ 1.0f };
    std::uint32_t stencil{ 0 };
};

using ClearValue = std::variant<ClearColor, ClearDepthStencil>;
}
