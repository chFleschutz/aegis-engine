export module aegis.render_graph:error;
import std;
import aegis.rhi;

export namespace aegis::render_graph
{
enum class ErrorCode
{
    Unknown,
    InvalidGraph,           // Passes or resources that cannot be scheduled: a cycle, a reference to nothing
    LimitExceeded,          // A fixed capacity is full, e.g. the number of draw batches
    Unsupported,            // The device lacks a feature the pass needs
    ShaderLoadFailed,       // A shader file is missing or not SPIR-V, or its pipeline could not be created
    ResourceCreationFailed, // A buffer, texture, view or sampler could not be created
    SubmitFailed,           // Recording, submitting or waiting for immediate work failed
    InitializationFailed,   // A pass or helper could not be set up, e.g. the IBL maps or ImGui's backend
    RhiFailed,              // An rhi call failed; the cause is in Error::rhi
};

struct Error
{
    ErrorCode code{ ErrorCode::Unknown };
    std::string message;
    std::optional<rhi::Error> rhi; // Set when an rhi call failed
};

[[nodiscard]] inline auto makeError(ErrorCode code, std::string message) -> std::unexpected<Error>
{
    return std::unexpected{ Error{ .code = code, .message = std::move(message) } };
}

/// @brief Wraps a failed rhi call; the code is ErrorCode::RhiFailed.
[[nodiscard]] inline auto makeError(const rhi::Error& error, std::string message) -> std::unexpected<Error>
{
    return std::unexpected{ Error{ .code = ErrorCode::RhiFailed, .message = std::move(message), .rhi = error } };
}

/// @brief Wraps a failed rhi call under a more specific code than ErrorCode::RhiFailed.
[[nodiscard]] inline auto makeError(ErrorCode code, const rhi::Error& error, std::string message) -> std::unexpected<Error>
{
    return std::unexpected{ Error{ .code = code, .message = std::move(message), .rhi = error } };
}

/// @brief Name of an error code as written by describe(), e.g. "InvalidGraph".
[[nodiscard]] constexpr auto errorCodeName(ErrorCode code) noexcept -> std::string_view
{
    switch (code)
    {
    case ErrorCode::Unknown: return "Unknown";
    case ErrorCode::InvalidGraph: return "InvalidGraph";
    case ErrorCode::LimitExceeded: return "LimitExceeded";
    case ErrorCode::Unsupported: return "Unsupported";
    case ErrorCode::ShaderLoadFailed: return "ShaderLoadFailed";
    case ErrorCode::ResourceCreationFailed: return "ResourceCreationFailed";
    case ErrorCode::SubmitFailed: return "SubmitFailed";
    case ErrorCode::InitializationFailed: return "InitializationFailed";
    case ErrorCode::RhiFailed: return "RhiFailed";
    }
    return "Unknown";
}

/// @brief One-line text for logs: "<code>: <message>", then " (caused by <rhi error>)" if an rhi call failed.
[[nodiscard]] inline auto describe(const Error& error) -> std::string
{
    auto text = std::format("{}: {}", errorCodeName(error.code), error.message);
    if (error.rhi)
        text += std::format(" (caused by {})", rhi::describe(*error.rhi));
    return text;
}
}
