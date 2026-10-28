module;

#include <glaze/glaze.hpp>

module aegis.editor.core;
import std;
import aegis.reflect;

namespace aegis::editor
{
namespace detail
{
// The clipboard's JSON (not in an anonymous namespace: glaze reflects a type with linkage).
struct CopiedFields
{
    std::string component;
    std::map<std::string, std::string> fields;
};
}

namespace
{
constexpr glz::opts readOptions{ .error_on_unknown_keys = false };
}

using detail::CopiedFields;

auto componentToText(const reflect::TypeInfo& type, const void* object) -> std::expected<std::string, std::string>
{
    CopiedFields copied{ .component = type.name };
    for (const auto& field : type.fields)
    {
        auto text = reflect::toText(field.kind, field.getter(object), field.enumEntries);
        if (!text)
            return std::unexpected{ std::format("cannot copy {}.{}: {}", type.name, field.name, reflect::describe(text.error())) };
        copied.fields.emplace(field.name, std::move(*text));
    }

    std::string json;
    if (const auto written = glz::write_json(copied, json); written)
        return std::unexpected{ std::format("cannot copy {}: {}", type.name, glz::format_error(written, json)) };
    return json;
}

auto componentFromText(const reflect::TypeInfo& type, std::string_view text) -> std::expected<FieldValues, std::string>
{
    CopiedFields copied;
    if (const auto parsed = glz::read<readOptions>(copied, text); parsed || copied.component.empty())
        return std::unexpected{ std::string{ "the clipboard holds no copied component" } };
    if (copied.component != type.name)
        return std::unexpected{ std::format("the clipboard holds a '{}', not a '{}'", copied.component, type.name) };

    FieldValues values;
    for (const auto& field : type.fields)
    {
        const auto copiedField = copied.fields.find(field.name);
        if (field.meta.readOnly || copiedField == copied.fields.end())
            continue;
        auto value = reflect::fromText(field.kind, copiedField->second, field.enumEntries);
        if (!value)
            return std::unexpected{ std::format("{}.{}: {}", type.name, field.name, reflect::describe(value.error())) };
        values.push_back({ .field = field.name, .value = std::move(*value) });
    }
    return values;
}
}
