module;
#include "aegis/core/flags.h"

export module aegis.rhi:common;
import std;

export namespace aegis::rhi
{
using TimelineValue = std::uint64_t;
using DeviceAddress = std::uint64_t;

enum class ErrorCode
{
    Unknown,
    DeviceLost,
    SurfaceLost,
    OutOfDate,
    OutOfHostMemory,
    OutOfDeviceMemory,
    InitializationFailed,
    NotReady,
    FormatNotSupported,
    OutOfDescriptors, // A bindless slot pool is exhausted; free views or samplers first, retrying alone cannot help
    InvalidArgument,  // A caller bug, e.g. upload data that does not match the image; retrying cannot help
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

/// @brief The enumerator's name, e.g. "RGBA16_SFLOAT", for tools and logs.
[[nodiscard]] constexpr auto formatName(Format format) noexcept -> std::string_view
{
    switch (format)
    {
    case Format::Unknown: return "Unknown";
    case Format::R8_UNORM: return "R8_UNORM";
    case Format::RG8_UNORM: return "RG8_UNORM";
    case Format::RGBA8_UNORM: return "RGBA8_UNORM";
    case Format::RGBA8_SRGB: return "RGBA8_SRGB";
    case Format::BGRA8_UNORM: return "BGRA8_UNORM";
    case Format::BGRA8_SRGB: return "BGRA8_SRGB";
    case Format::RGB10A2_UNORM: return "RGB10A2_UNORM";
    case Format::B10G11R11_UFLOAT: return "B10G11R11_UFLOAT";
    case Format::R16_UNORM: return "R16_UNORM";
    case Format::RG16_UNORM: return "RG16_UNORM";
    case Format::RGBA16_UNORM: return "RGBA16_UNORM";
    case Format::RGBA16_SFLOAT: return "RGBA16_SFLOAT";
    case Format::R32_SFLOAT: return "R32_SFLOAT";
    case Format::RG32_SFLOAT: return "RG32_SFLOAT";
    case Format::RGB32_SFLOAT: return "RGB32_SFLOAT";
    case Format::RGBA32_SFLOAT: return "RGBA32_SFLOAT";
    case Format::D32_SFLOAT: return "D32_SFLOAT";
    case Format::D24_UNORM_S8_UINT: return "D24_UNORM_S8_UINT";
    case Format::D32_SFLOAT_S8_UINT: return "D32_SFLOAT_S8_UINT";
    }
    return "Unknown";
}

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

/// @brief The ways a resource is used, each mapping to a layout plus the stages and accesses that
///        synchronize it. Barriers are formed from a pair: the resource's previous and next state.
enum class ResourceState
{
    Unknown, // Contents are discarded; waits for all prior work, so a reused resource is safe
    ColorAttachment,
    DepthWrite,
    DepthRead,
    ShaderReadVertex,
    ShaderReadFragment,
    ShaderReadCompute,
    ShaderRead,   // Any shader stage, e.g. a texture whose readers are not known when it is uploaded
    IndirectRead, // Buffers only: indirect draw / dispatch arguments
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

enum class CompareOp
{
    Never,
    Less,
    Equal,
    LessOrEqual,
    Greater,
    NotEqual,
    GreaterOrEqual,
    Always,
};

enum class CullMode
{
    None,
    Front,
    Back,
    FrontAndBack,
};

enum class FrontFace
{
    CounterClockwise,
    Clockwise,
};

/// @brief How a fragment's color combines with the attachment's.
enum class BlendMode
{
    None,     // Overwrite
    Alpha,    // Straight alpha: src * srcAlpha + dst * (1 - srcAlpha)
    Additive, // src + dst
};

// clang-format off
enum class BufferUsage : std::uint32_t
{
    None                = 0,
    Vertex              = 1 << 0,
    Index               = 1 << 1,
    Uniform             = 1 << 2,
    Storage             = 1 << 3,
    Indirect            = 1 << 4,
    TransferSrc         = 1 << 5,
    TransferDst         = 1 << 6,
    ShaderDeviceAddress = 1 << 7, // Enables Buffer::deviceAddress()
};
// clang-format on

AEGIS_FLAGS(BufferUsage)

/// @brief Dimensionality of an image. Cube is a 2D image whose array layers are cube faces (a multiple
///        of 6, square extent); it is what makes an image cube-compatible.
enum class ImageType
{
    Dim1D,
    Dim2D,
    Dim3D,
    Cube,
};

/// @brief How a view interprets an image. Must match what the shader declares, e.g. Texture2D is
///        Dim2D, TextureCube is Cube, and RWTexture2DArray over cube faces is Dim2DArray.
enum class ImageViewType
{
    Dim1D,
    Dim2D,
    Dim3D,
    Cube,
    Dim1DArray,
    Dim2DArray,
    CubeArray,
};

// clang-format off
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
// clang-format on

AEGIS_FLAGS(ImageUsage)

enum class MemoryUsage
{
    GpuOnly,  // Device local, no CPU access
    CpuWrite, // Persistent map, sequential write (staging, uniforms)
    CpuRead,  // Persistent map, random read + cached (readback)
};

enum class Filter
{
    Nearest,
    Linear,
};

enum class SamplerMipmapMode
{
    Nearest,
    Linear,
};

enum class SamplerAddressMode
{
    Repeat,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder, // Border is always opaque black
};

/// @brief Where a timestamp is taken: once all previously submitted commands have reached that
///        stage. Bracket timed work with TopOfPipe before it and AllCommands after it.
enum class TimestampStage
{
    TopOfPipe,
    AllCommands,
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

    constexpr Extent2D(std::uint32_t x, std::uint32_t y) :
        x{ x },
        y{ y }
    {
    }

    explicit constexpr Extent2D(std::pair<std::uint32_t, std::uint32_t> pair) :
        x{ pair.first },
        y{ pair.second }
    {
    }

    constexpr auto operator==(const Extent2D&) const -> bool = default;
};

struct Extent3D
{
    std::uint32_t x;
    std::uint32_t y;
    std::uint32_t z;

    constexpr Extent3D(std::uint32_t x, std::uint32_t y, std::uint32_t z) :
        x{ x },
        y{ y },
        z{ z }
    {
    }

    explicit constexpr Extent3D(Extent2D extent) :
        x{ extent.x },
        y{ extent.y },
        z{ 1 }
    {
    }

    constexpr auto operator==(const Extent3D&) const -> bool = default;

    [[nodiscard]] constexpr auto toExtent2D() const noexcept -> Extent2D { return { x, y }; }
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
