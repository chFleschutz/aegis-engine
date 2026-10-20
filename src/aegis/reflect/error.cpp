module aegis.reflect;
import std;

namespace aegis::reflect
{
namespace
{
constexpr auto codeName(ErrorCode code) -> std::string_view
{
    switch (code)
    {
    case ErrorCode::InvalidName: return "InvalidName";
    case ErrorCode::DuplicateName: return "DuplicateName";
    case ErrorCode::DuplicateType: return "DuplicateType";
    case ErrorCode::UnknownType: return "UnknownType";
    case ErrorCode::UnknownField: return "UnknownField";
    case ErrorCode::TypeMismatch: return "TypeMismatch";
    case ErrorCode::OutOfRange: return "OutOfRange";
    case ErrorCode::ParseError: return "ParseError";
    case ErrorCode::WrongCategory: return "WrongCategory";
    case ErrorCode::NotConstructible: return "NotConstructible";
    }
    return "Unknown";
}
}

auto describe(const Error& error) -> std::string
{
    return std::format("{}: {}", codeName(error.code), error.message);
}
}
