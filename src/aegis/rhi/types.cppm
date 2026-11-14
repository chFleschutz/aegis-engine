export module aegis.rhi.types;
import std;

// The rhi value types that code without a GPU names: the error, a buffer's address and the descriptors a shader
// reads. A module of its own, without vulkan, so that such code (aegis.assets.data) imports it instead of
// aegis.rhi and is not rebuilt when an rhi interface changes. aegis.rhi re-exports it. Keep it small and
// stable: everything that imports it rebuilds when it changes. Formats, usage flags and extents stay in
// aegis.rhi:common.

export namespace aegis::rhi
{
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

struct Error
{
    ErrorCode code;
#ifndef NDEBUG
    std::source_location location;
#endif
};

// clang-format off: it can't lay out a parameter list split by #ifndef
[[nodiscard]] inline auto makeError(ErrorCode code
#ifndef NDEBUG
    ,
    std::source_location loc = std::source_location::current()
#endif
) noexcept -> std::unexpected<Error>
// clang-format on
{
    return std::unexpected{
        Error{
            .code = code,
#ifndef NDEBUG
            .location = std::move(loc)
#endif
        }
    };
}

/// @brief Name of an error code as written by describe(), e.g. "OutOfDate".
[[nodiscard]] constexpr auto errorCodeName(ErrorCode code) noexcept -> std::string_view
{
    switch (code)
    {
    case ErrorCode::Unknown: return "Unknown";
    case ErrorCode::DeviceLost: return "DeviceLost";
    case ErrorCode::SurfaceLost: return "SurfaceLost";
    case ErrorCode::OutOfDate: return "OutOfDate";
    case ErrorCode::OutOfHostMemory: return "OutOfHostMemory";
    case ErrorCode::OutOfDeviceMemory: return "OutOfDeviceMemory";
    case ErrorCode::InitializationFailed: return "InitializationFailed";
    case ErrorCode::NotReady: return "NotReady";
    case ErrorCode::FormatNotSupported: return "FormatNotSupported";
    case ErrorCode::OutOfDescriptors: return "OutOfDescriptors";
    case ErrorCode::InvalidArgument: return "InvalidArgument";
    }
    return "Unknown";
}

/// @brief One-line text for logs: the code name, plus "at file:line (function)" in debug builds.
[[nodiscard]] inline auto describe(const Error& error) -> std::string
{
#ifndef NDEBUG
    return std::format("{} at {}:{} ({})",
        errorCodeName(error.code),
        error.location.file_name(),
        error.location.line(),
        error.location.function_name());
#else
    return std::string{ errorCodeName(error.code) };
#endif
}

/// @brief Describes all descriptor types.
/// @note Buffers are not accessed via descriptors and use buffer device address instead.
enum class DescriptorType
{
    SampledImage,
    StorageImage,
    Sampler
};

template<DescriptorType T>
struct Descriptor
{
    std::uint32_t index;
};

using SampledImageDescriptor = Descriptor<DescriptorType::SampledImage>;
using StorageImageDescriptor = Descriptor<DescriptorType::StorageImage>;
using SamplerDescriptor = Descriptor<DescriptorType::Sampler>;

/// @brief A sampled image plus the sampler to read it with: bindless::TextureDescriptor in Slang, 8 bytes
///        (image index, then sampler index). How an assets::Texture reaches a shader, and the layout of
///        a texture parameter in a material's block.
struct TextureDescriptor
{
    SampledImageDescriptor image;
    SamplerDescriptor sampler;
};
}
