module aegis.serialize;
import std;
import aegis.reflect;

namespace aegis::serialize
{
namespace
{
constexpr auto codeName(ErrorCode code) -> std::string_view
{
    switch (code)
    {
    case ErrorCode::Syntax: return "Syntax";
    case ErrorCode::InvalidData: return "InvalidData";
    case ErrorCode::WrongType: return "WrongType";
    case ErrorCode::UnknownType: return "UnknownType";
    case ErrorCode::UnknownField: return "UnknownField";
    case ErrorCode::InvalidValue: return "InvalidValue";
    case ErrorCode::UnsupportedVersion: return "UnsupportedVersion";
    case ErrorCode::DuplicateEntity: return "DuplicateEntity";
    case ErrorCode::MissingEntity: return "MissingEntity";
    case ErrorCode::Cycle: return "Cycle";
    case ErrorCode::MissingScene: return "MissingScene";
    }
    return "Unknown";
}
}

auto describe(const Error& error) -> std::string
{
    std::string text = error.line > 0 ? std::format("line {}: ", error.line) : std::string{};
    text += std::format("{}: {}", codeName(error.code), error.message);
    if (error.reflect)
        text += std::format(" ({})", reflect::describe(*error.reflect));
    return text;
}
}
