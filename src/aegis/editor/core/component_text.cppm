export module aegis.editor.core:component_text;
import std;
import aegis.reflect;
import :edits;

// The text a component's or script's fields copy to the clipboard and paste from: a JSON object naming the type and
// holding each field's text form (reflect::toText), so only a paste onto the same type applies.
//
//   {"component":"Transform","fields":{"location":"0 1 0","rotation":"0 0 0 1","scale":"1 1 1"}}
export namespace aegis::editor
{
/// @brief The clipboard text of the fields of 'object', an instance of 'type'.
[[nodiscard]] auto componentToText(const reflect::TypeInfo& type, const void* object) -> std::expected<std::string, std::string>;

/// @brief The values 'text' holds for the fields of 'type', in registration order: those it names, except the
///        read-only fields. Names that are no field of the type are ignored.
/// @return Why the text is no fields of 'type': it is no copied component, it holds another type, or the text form of
///         a field's value is invalid; nothing is returned then
[[nodiscard]] auto componentFromText(const reflect::TypeInfo& type, std::string_view text) -> std::expected<FieldValues, std::string>;
}
