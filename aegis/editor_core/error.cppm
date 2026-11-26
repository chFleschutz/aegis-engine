export module aegis.editor.core:error;
import std;
import aegis.reflect;
import aegis.serialize;
import aegis.world;

export namespace aegis::editor
{
enum class ErrorCode
{
    NotEditable,        // An entity to edit is null, destroyed or part of a scene instance
    MissingEntity,      // An entity an edit names by its StableId is gone, or a target entity is null or destroyed
    UnknownType,        // No type is registered under the name an edit or a script names
    UnknownField,       // The type has no field of the name an edit names
    WrongCategory,      // The type is not a component with storage, or not a script, where one is needed
    MissingComponent,   // The entity lacks the component or script to change, remove or detach
    DuplicateComponent, // The entity already has the component an edit adds
    RequiredComponent,  // The component is required, so it cannot be removed
    WrongComponent,     // Pasted fields of another component than the one pasted onto
    InvalidPath,        // A typed scene file name that is no path in the content folder
    FileExists,         // Save As's file exists already
    FileFailed,         // A file or folder could not be opened, read, created, written or replaced
    JsonFailed,         // JSON that does not parse or cannot be written (preferences, recent scenes, copied fields)
    SerializeFailed,    // Capturing or instantiating a scene failed; the cause is in Error::serialize
    ReflectFailed,      // The type registry refused; the cause is in Error::reflect
    HierarchyFailed,    // The hierarchy refused a change; the cause is in Error::hierarchy
    AssetFailed,        // The asset library refused; its text is in Error::assets
    MissingMaterial,    // A material file an edit names is not open in the editor (MaterialDocuments)
};

struct Error
{
    ErrorCode code;
    std::string message;
    std::optional<serialize::Error> serialize;      // Set when a scene capture or instantiation is the cause
    std::optional<reflect::Error> reflect;          // Set when the type registry is the cause
    std::optional<world::HierarchyError> hierarchy; // Set when the hierarchy is the cause
    std::string assets;                             // The asset library's error text, until it returns a typed error (#342)
};

[[nodiscard]] inline auto makeError(ErrorCode code, std::string message) -> std::unexpected<Error>
{
    return std::unexpected{ Error{ .code = code, .message = std::move(message) } };
}

/// @brief Wraps a failed scene capture or instantiation; the code is ErrorCode::SerializeFailed.
[[nodiscard]] inline auto makeError(const serialize::Error& cause, std::string message) -> std::unexpected<Error>
{
    return std::unexpected{ Error{ .code = ErrorCode::SerializeFailed, .message = std::move(message), .serialize = cause } };
}

/// @brief Wraps a refusal of the type registry; the code is ErrorCode::ReflectFailed.
[[nodiscard]] inline auto makeError(const reflect::Error& cause, std::string message) -> std::unexpected<Error>
{
    return std::unexpected{ Error{ .code = ErrorCode::ReflectFailed, .message = std::move(message), .reflect = cause } };
}

/// @brief Wraps a refusal of the hierarchy; the code is ErrorCode::HierarchyFailed.
[[nodiscard]] inline auto makeError(world::HierarchyError cause, std::string message) -> std::unexpected<Error>
{
    return std::unexpected{ Error{ .code = ErrorCode::HierarchyFailed, .message = std::move(message), .hierarchy = cause } };
}

/// @brief Wraps the asset library's error text; the code is ErrorCode::AssetFailed.
[[nodiscard]] inline auto makeAssetError(std::string cause, std::string message) -> std::unexpected<Error>
{
    return std::unexpected{ Error{ .code = ErrorCode::AssetFailed, .message = std::move(message), .assets = std::move(cause) } };
}

/// @brief Name of an error code as written by describe(), e.g. "NotEditable".
[[nodiscard]] auto errorCodeName(ErrorCode code) noexcept -> std::string_view;

/// @brief The one place an Error is turned into text: "<code>: <message> (<cause>)".
[[nodiscard]] auto describe(const Error& error) -> std::string;
}
