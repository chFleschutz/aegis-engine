module;

#include <glaze/glaze.hpp>

module aegis.serialize;
import std;
import aegis.reflect;

namespace aegis::serialize
{
namespace
{
using reflect::Node;

constexpr glz::opts readOptions{ .comments = true };

using RawObject = std::vector<std::pair<std::string, glz::raw_json_view>>;
using RawArray = std::vector<glz::raw_json_view>;

auto isSpace(char c) -> bool
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// JSON's number grammar: -?(0|[1-9][0-9]*)(.[0-9]+)?([eE][+-]?[0-9]+)?
auto isNumber(std::string_view text) -> bool
{
    auto digits = [&text] {
        const std::size_t count = std::ranges::find_if_not(text, [](char c) { return c >= '0' && c <= '9'; }) - text.begin();
        text.remove_prefix(count);
        return count;
    };
    if (text.starts_with('-'))
        text.remove_prefix(1);
    if (text.starts_with('0'))
        text.remove_prefix(1);
    else if (digits() == 0)
        return false;
    if (text.starts_with('.'))
    {
        text.remove_prefix(1);
        if (digits() == 0)
            return false;
    }
    if (text.starts_with('e') || text.starts_with('E'))
    {
        text.remove_prefix(1);
        if (text.starts_with('+') || text.starts_with('-'))
            text.remove_prefix(1);
        if (digits() == 0)
            return false;
    }
    return text.empty();
}

// The position after the whitespace and comments at `pos`.
auto skipSpace(std::string_view text, std::size_t pos) -> std::size_t
{
    while (pos < text.size())
    {
        if (isSpace(text[pos]))
        {
            ++pos;
        }
        else if (text.substr(pos, 2) == "//")
        {
            pos = text.find('\n', pos);
            if (pos == std::string_view::npos)
                return text.size();
        }
        else if (text.substr(pos, 2) == "/*")
        {
            pos = text.find("*/", pos + 2);
            if (pos == std::string_view::npos)
                return text.size();
            pos += 2;
        }
        else
        {
            break;
        }
    }
    return pos;
}

// Replaces each trailing comma (one followed, past whitespace and comments, by '}' or ']') with a space: glaze
// reads comments but not trailing commas. The text keeps its length, so positions stay where they were.
auto blankTrailingCommas(std::string& text) -> void
{
    std::optional<std::size_t> comma;
    std::size_t pos = 0;
    while (pos < text.size())
    {
        const std::size_t next = skipSpace(text, pos);
        if (next != pos)
        {
            pos = next;
            continue;
        }

        const char c = text[pos];
        if (c == '"')
        {
            // Skip the string, escapes included; an unclosed one is glaze's error to report
            for (++pos; pos < text.size() && text[pos] != '"'; ++pos)
            {
                if (text[pos] == '\\')
                    ++pos;
            }
        }
        else if (c == ',')
        {
            comma = pos;
            ++pos;
            continue;
        }
        else if ((c == '}' || c == ']') && comma)
        {
            text[*comma] = ' ';
        }
        comma.reset();
        ++pos;
    }
}

class Reader
{
public:
    explicit Reader(std::string_view text) :
        m_text{ text }
    {
        m_lineStarts.push_back(0);
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '\n')
                m_lineStarts.push_back(i + 1);
        }
    }

    auto readDocument() -> std::expected<Node, Error>
    {
        const std::size_t start = skipSpace(m_text, 0);
        if (start == m_text.size())
            return makeError(ErrorCode::Syntax, "the file is empty", line(start));

        glz::raw_json_view raw;
        if (auto ec = glz::read<readOptions>(raw, m_text.substr(start)))
            return syntaxError(ec, start);
        const std::size_t end = static_cast<std::size_t>(raw.str.data() - m_text.data()) + raw.str.size();
        if (const std::size_t rest = skipSpace(m_text, end); rest != m_text.size())
            return makeError(ErrorCode::Syntax, "unexpected text after the end of the document", line(rest));
        return read(raw.str);
    }

private:
    // `value` is one value inside m_text, starting at its first character.
    auto read(std::string_view value) -> std::expected<Node, Error>
    {
        const std::size_t offset = static_cast<std::size_t>(value.data() - m_text.data());
        Node node;
        node.line = line(offset);

        const char first = value.empty() ? '\0' : value.front();
        if (first == '{')
        {
            RawObject members;
            if (auto ec = glz::read<readOptions>(members, value))
                return syntaxError(ec, offset);
            node.kind = Node::Kind::Object;
            for (auto& [key, raw] : members)
            {
                auto member = read(raw.str);
                if (!member)
                    return member;
                if (node.find(key))
                    return makeError(ErrorCode::Syntax, std::format("the key '{}' appears twice", key), member->line);
                node.add(std::move(key), std::move(*member));
            }
        }
        else if (first == '[')
        {
            RawArray items;
            if (auto ec = glz::read<readOptions>(items, value))
                return syntaxError(ec, offset);
            node.kind = Node::Kind::Array;
            for (const auto& raw : items)
            {
                auto item = read(raw.str);
                if (!item)
                    return item;
                node.push(std::move(*item));
            }
        }
        else if (first == '"')
        {
            node.kind = Node::Kind::String;
            if (auto ec = glz::read<readOptions>(node.text, value))
                return syntaxError(ec, offset);
        }
        else if (value == "true" || value == "false")
        {
            node = Node::makeBool(value == "true");
            node.line = line(offset);
        }
        else if (value == "null")
        {
            node.kind = Node::Kind::Null;
        }
        else
        {
            if (!isNumber(value))
                return makeError(ErrorCode::Syntax, std::format("'{}' is not a JSON value", value), node.line);
            node.kind = Node::Kind::Number;
            node.text = std::string{ value };
        }
        return node;
    }

    auto line(std::size_t offset) const -> std::uint32_t
    {
        auto next = std::ranges::upper_bound(m_lineStarts, offset);
        return static_cast<std::uint32_t>(next - m_lineStarts.begin());
    }

    auto syntaxError(const glz::error_ctx& ec, std::size_t offset) const -> std::unexpected<Error>
    {
        std::string message{ glz::format_error(ec.ec) };
        if (!ec.custom_error_message.empty())
            message += std::format(": {}", ec.custom_error_message);
        return makeError(ErrorCode::Syntax, std::move(message), line(std::min(offset + ec.count, m_text.size())));
    }

    std::string_view m_text;
    std::vector<std::size_t> m_lineStarts; // The offset of each line's first character
};

auto isScalar(const Node& node) -> bool
{
    return node.kind != Node::Kind::Array && node.kind != Node::Kind::Object;
}

auto quote(const std::string& text) -> std::string
{
    std::string quoted;
    [[maybe_unused]] auto ec = glz::write_json(text, quoted);
    return quoted;
}

auto write(std::string& out, const Node& node, std::size_t indent) -> void
{
    switch (node.kind)
    {
    case Node::Kind::Null: out += "null"; return;
    case Node::Kind::Bool: out += node.boolean ? "true" : "false"; return;
    case Node::Kind::Number: out += node.text; return;
    case Node::Kind::String: out += quote(node.text); return;
    case Node::Kind::Array:
        if (node.items.empty())
        {
            out += "[]";
        }
        else if (std::ranges::all_of(node.items, isScalar))
        {
            out += '[';
            for (std::size_t i = 0; i < node.items.size(); ++i)
            {
                if (i > 0)
                    out += ", ";
                write(out, node.items[i], indent);
            }
            out += ']';
        }
        else
        {
            out += "[\n";
            for (std::size_t i = 0; i < node.items.size(); ++i)
            {
                out.append(indent + 2, ' ');
                write(out, node.items[i], indent + 2);
                out += i + 1 < node.items.size() ? ",\n" : "\n";
            }
            out.append(indent, ' ');
            out += ']';
        }
        return;
    case Node::Kind::Object:
        if (node.items.empty())
        {
            out += "{}";
            return;
        }
        out += "{\n";
        for (std::size_t i = 0; i < node.items.size(); ++i)
        {
            out.append(indent + 2, ' ');
            out += quote(node.items[i].key);
            out += ": ";
            write(out, node.items[i], indent + 2);
            out += i + 1 < node.items.size() ? ",\n" : "\n";
        }
        out.append(indent, ' ');
        out += '}';
        return;
    }
}
}

auto parseJson(std::string_view text) -> std::expected<reflect::Node, Error>
{
    std::string buffer{ text };
    blankTrailingCommas(buffer);
    return Reader{ buffer }.readDocument();
}

auto writeJson(const reflect::Node& node) -> std::string
{
    std::string out;
    write(out, node, 0);
    out += '\n';
    return out;
}
}
