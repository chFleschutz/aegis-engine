module aegis.editor.core;
import std;
import aegis.reflect;
import aegis.serialize;
import aegis.world;

namespace aegis::editor
{
auto errorCodeName(ErrorCode code) noexcept -> std::string_view
{
    switch (code)
    {
    case ErrorCode::NotEditable: return "NotEditable";
    case ErrorCode::MissingEntity: return "MissingEntity";
    case ErrorCode::UnknownType: return "UnknownType";
    case ErrorCode::UnknownField: return "UnknownField";
    case ErrorCode::WrongCategory: return "WrongCategory";
    case ErrorCode::MissingComponent: return "MissingComponent";
    case ErrorCode::DuplicateComponent: return "DuplicateComponent";
    case ErrorCode::RequiredComponent: return "RequiredComponent";
    case ErrorCode::WrongComponent: return "WrongComponent";
    case ErrorCode::InvalidPath: return "InvalidPath";
    case ErrorCode::FileExists: return "FileExists";
    case ErrorCode::FileFailed: return "FileFailed";
    case ErrorCode::JsonFailed: return "JsonFailed";
    case ErrorCode::SerializeFailed: return "SerializeFailed";
    case ErrorCode::ReflectFailed: return "ReflectFailed";
    case ErrorCode::HierarchyFailed: return "HierarchyFailed";
    case ErrorCode::AssetFailed: return "AssetFailed";
    case ErrorCode::MissingMaterial: return "MissingMaterial";
    case ErrorCode::InstanceCycle: return "InstanceCycle";
    }
    return "Unknown";
}

auto describe(const Error& error) -> std::string
{
    std::string text{ errorCodeName(error.code) };
    if (!error.message.empty())
        text += std::format(": {}", error.message);
    if (error.serialize)
        text += std::format(" ({})", serialize::describe(*error.serialize));
    if (error.reflect)
        text += std::format(" ({})", reflect::describe(*error.reflect));
    if (error.hierarchy)
        text += std::format(" ({})", world::describe(*error.hierarchy));
    if (!error.assets.empty())
        text += std::format(" ({})", error.assets);
    return text;
}
}
