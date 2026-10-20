export module aegis.reflect:error;
import std;

export namespace aegis::reflect
{
enum class ErrorCode
{
    InvalidName,      // An empty type name
    DuplicateName,    // A type or field name is already registered
    DuplicateType,    // The C++ type is already registered (under another name)
    UnknownType,      // No type registered under this name
    UnknownField,     // The type has no field of this name
    TypeMismatch,     // A Value of the wrong kind for the field
    OutOfRange,       // The value does not fit the field's type, or is not one of an enum's values
    ParseError,       // Text that is not a valid value of the field's kind
    NotConstructible, // The type cannot be created by name (it is not default constructible)
};

struct Error
{
    ErrorCode code;
    std::string message;
};

[[nodiscard]] inline auto makeError(ErrorCode code, std::string message) -> std::unexpected<Error>
{
    return std::unexpected{ Error{ .code = code, .message = std::move(message) } };
}

// The one place an Error is turned into text.
[[nodiscard]] auto describe(const Error& error) -> std::string;
}
