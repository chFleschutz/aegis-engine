export module aegis.serialize:json;
import std;
import aegis.reflect;
import :error;

// The one place the text format is chosen: JSON, read through glaze. Everything above works on reflect::Node.
export namespace aegis::serialize
{
// Reads JSON text into a node tree. Comments and trailing commas (JSONC) are accepted; every node records the
// line it starts on, for errors about it. A key that appears twice in one object is an error.
// Fails with ErrorCode::Syntax, at the line of the error.
[[nodiscard]] auto parseJson(std::string_view text) -> std::expected<reflect::Node, Error>;

// Writes a node tree as JSON in one fixed layout, so the same tree always gives the same bytes:
//  - two-space indentation, one object member per line, members in node order;
//  - an array of numbers, strings, booleans and nulls on one line ("[1, 2, 3]"), any other array one element
//    per line; empty objects and arrays as {} and [];
//  - numbers as their node text, strings escaped by the JSON rules;
//  - a final newline. No comments, no trailing commas.
[[nodiscard]] auto writeJson(const reflect::Node& node) -> std::string;
}
