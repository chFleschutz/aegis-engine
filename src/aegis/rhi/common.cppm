export module aegis.rhi:common;

export namespace aegis::rhi
{
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

enum class ImageLayout
{
    Unknown,
    General,
    Attachment,
    ReadOnly,
    TransferSrc,
    TransferDst,
    Present,
};

enum class ResourceState
{
    Unknown,
    RenderTarget,
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

}
