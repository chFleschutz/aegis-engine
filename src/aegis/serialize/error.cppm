export module aegis.serialize:error;
import std;
import aegis.reflect;

export namespace aegis::serialize
{
enum class ErrorCode
{
    Syntax,             // Not JSON (comments and trailing commas are fine)
    InvalidData,        // JSON of the wrong shape: a missing, unknown or mistyped key, a bad ID
    WrongType,          // A file of another type than the one asked for (its header's "type")
    UnknownType,        // A component or script type that is not registered
    UnknownField,       // A field its type does not have
    InvalidValue,       // A field value of the wrong kind, or one that does not fit the field
    UnsupportedVersion, // A version newer than the engine knows, or one no migration upgrades
    DuplicateEntity,    // An entity listed twice (in the entity list, or as a child)
    MissingEntity,      // A child ID that no entity has
    Cycle,              // An entity among its own descendants
};

struct Error
{
    ErrorCode code;
    std::string message;
    std::uint32_t line{ 0 };               // The 1-based line in the file the error is about; 0 if none
    std::optional<reflect::Error> reflect; // The type registry's error, when it is the cause
};

[[nodiscard]] inline auto makeError(ErrorCode code, std::string message, std::uint32_t line = 0) -> std::unexpected<Error>
{
    return std::unexpected{ Error{ .code = code, .message = std::move(message), .line = line } };
}

[[nodiscard]] inline auto makeError(ErrorCode code, const reflect::Error& cause, std::string message, std::uint32_t line = 0)
    -> std::unexpected<Error>
{
    return std::unexpected{ Error{ .code = code, .message = std::move(message), .line = line, .reflect = cause } };
}

// The one place an Error is turned into text: "line 12: InvalidValue: ... (TypeMismatch: ...)".
[[nodiscard]] auto describe(const Error& error) -> std::string;
}
