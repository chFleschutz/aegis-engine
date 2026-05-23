module;
#include <cstdint>
#include <utility>
#include <variant>
#include <limits>

export module aegis.rhi:common;

export namespace aegis::rhi
{
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

struct DeviceCapabilities
{
    bool meshShaders = false;
};

struct QueueFamilyIndices
{
    static constexpr std::uint32_t familyIgnored{ std::numeric_limits<std::uint32_t>::max() };

    uint32_t graphics{ familyIgnored };
    uint32_t compute{ familyIgnored };
    uint32_t transfer{ familyIgnored };
    uint32_t present{ familyIgnored };

    [[nodiscard]] auto isComplete() const -> bool
    {
        return graphics != familyIgnored && compute != familyIgnored &&
               transfer != familyIgnored && present != familyIgnored;
    }
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
