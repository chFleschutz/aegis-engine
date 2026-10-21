#include <doctest/doctest.h>

import std;
import aegis.reflect;
import aegis.serialize;

namespace
{
using aegis::reflect::Node;
using aegis::serialize::ErrorCode;
using aegis::serialize::parseJson;
using aegis::serialize::writeJson;
}

TEST_SUITE("serialize::parseJson")
{
    TEST_CASE("reads every kind of value")
    {
        auto node = parseJson(R"({"b": true, "n": null, "x": -1.5e2, "s": "a\"b\n", "a": [1, "two"], "o": {}})");
        REQUIRE(node.has_value());
        REQUIRE(node->isObject());
        CHECK(node->find("b")->kind == Node::Kind::Bool);
        CHECK(node->find("b")->boolean);
        CHECK(node->find("n")->isNull());
        CHECK(node->find("x")->kind == Node::Kind::Number);
        CHECK(node->find("x")->text == "-1.5e2");
        CHECK(node->find("s")->text == "a\"b\n");
        REQUIRE(node->find("a")->items.size() == 2);
        CHECK(node->find("a")->items[1].text == "two");
        CHECK(node->find("o")->isObject());
        CHECK(node->find("o")->items.empty());
    }

    TEST_CASE("keeps the members in file order")
    {
        auto node = parseJson(R"({"z": 1, "a": 2, "m": 3})");
        REQUIRE(node.has_value());
        REQUIRE(node->items.size() == 3);
        CHECK(node->items[0].key == "z");
        CHECK(node->items[1].key == "a");
        CHECK(node->items[2].key == "m");
    }

    TEST_CASE("records the line each value starts on")
    {
        auto node = parseJson("{\n  \"a\": 1,\n  \"b\": [\n    2,\n    3\n  ]\n}\n");
        REQUIRE(node.has_value());
        CHECK(node->line == 1);
        CHECK(node->find("a")->line == 2);
        CHECK(node->find("b")->line == 3);
        CHECK(node->find("b")->items[1].line == 5);
    }

    TEST_CASE("counts lines the same with Windows line ends")
    {
        auto node = parseJson("{\r\n  \"a\": 1,\r\n  \"b\": 2\r\n}\r\n");
        REQUIRE(node.has_value());
        CHECK(node->find("b")->line == 3);
        CHECK(node->find("b")->text == "2");
    }

    TEST_CASE("accepts comments and trailing commas")
    {
        auto node = parseJson(R"({
            // A comment
            "a": [1, 2, /* inline */ ],
            "b": { "c": "a, ] in a string", },
            /* before the end */
        })");
        REQUIRE(node.has_value());
        CHECK(node->find("a")->items.size() == 2);
        CHECK(node->find("b")->find("c")->text == "a, ] in a string");
    }

    TEST_CASE("reports a syntax error at its line")
    {
        auto node = parseJson("{\n  \"a\": 1,\n  \"b\": tru\n}");
        REQUIRE_FALSE(node.has_value());
        CHECK(node.error().code == ErrorCode::Syntax);
        CHECK(node.error().line == 3);
    }

    TEST_CASE("rejects a key that appears twice")
    {
        auto node = parseJson("{\n  \"a\": 1,\n  \"a\": 2\n}");
        REQUIRE_FALSE(node.has_value());
        CHECK(node.error().code == ErrorCode::Syntax);
        CHECK(node.error().line == 3);
    }

    TEST_CASE("rejects an empty file and text after the document")
    {
        CHECK(parseJson("  // nothing\n").error().code == ErrorCode::Syntax);
        auto extra = parseJson("{}\n{}");
        REQUIRE_FALSE(extra.has_value());
        CHECK(extra.error().code == ErrorCode::Syntax);
        CHECK(extra.error().line == 2);
    }

    TEST_CASE("rejects a malformed number")
    {
        CHECK(parseJson(R"({"a": 1.2.3})").error().code == ErrorCode::Syntax);
        CHECK(parseJson(R"({"a": -})").error().code == ErrorCode::Syntax);
    }
}

TEST_SUITE("serialize::writeJson")
{
    TEST_CASE("writes the fixed layout")
    {
        Node root = Node::makeObject();
        root.add("name", Node::makeString("a \"quoted\" name"));
        root.add("flag", Node::makeBool(false));
        root.add("none", Node{});
        Node& vector = root.add("vector", Node::makeArray());
        vector.push(Node::makeNumber(std::string{ "1" }));
        vector.push(Node::makeNumber(std::string{ "0.5" }));
        root.add("empty", Node::makeArray());
        root.add("nothing", Node::makeObject());
        Node& list = root.add("list", Node::makeArray());
        list.push(Node::makeObject()).add("x", Node::makeNumber(std::string{ "1" }));
        list.push(Node::makeArray());

        CHECK(writeJson(root) == R"({
  "name": "a \"quoted\" name",
  "flag": false,
  "none": null,
  "vector": [1, 0.5],
  "empty": [],
  "nothing": {},
  "list": [
    {
      "x": 1
    },
    []
  ]
}
)");
    }

    TEST_CASE("writes what it reads unchanged")
    {
        const std::string text = R"({
  "a": [1, 2.5, -3e-07],
  "b": {
    "c": "line\nbreak",
    "d": [true, null, "x"]
  }
}
)";
        auto node = parseJson(text);
        REQUIRE(node.has_value());
        CHECK(writeJson(*node) == text);
    }
}
