#include <doctest/doctest.h>

import std;
import aegis.math;
import aegis.reflect;

namespace
{
using namespace aegis::reflect;

const auto modeEntries = std::to_array<EnumEntry>({ { "Off", 0 }, { "On", 3 } });

// Writes `value` as a node, reads it back, and expects the same node again.
auto roundTrip(FieldKind kind, const Value& value) -> std::pair<Node, Value>
{
    auto node = toNode(kind, value, modeEntries);
    REQUIRE(node.has_value());
    auto read = fromNode(kind, *node, modeEntries);
    REQUIRE(read.has_value());
    return { *node, *read };
}

auto numbers(std::initializer_list<std::string_view> texts) -> Node
{
    Node array = Node::makeArray();
    for (auto text : texts)
        array.push(Node::makeNumber(std::string{ text }));
    return array;
}
}

TEST_SUITE("reflect::Node")
{
    TEST_CASE("finds, adds, renames and removes object members")
    {
        Node object = Node::makeObject();
        object.add("a", Node::makeBool(true));
        object.add("b", Node::makeString("text"));

        REQUIRE(object.find("a") != nullptr);
        CHECK(object.find("a")->boolean);
        CHECK(object.find("a")->key == "a");
        CHECK(object.find("missing") == nullptr);

        CHECK(object.rename("b", "c"));
        CHECK(object.find("b") == nullptr);
        REQUIRE(object.find("c") != nullptr);
        CHECK(object.find("c")->text == "text");
        CHECK_FALSE(object.rename("b", "d"));

        object.set("c", Node::makeNumber(std::string{ "1" }));
        CHECK(object.find("c")->text == "1");
        CHECK(object.find("c")->key == "c");
        object.set("d", Node{});
        CHECK(object.items.size() == 3);

        CHECK(object.remove("a"));
        CHECK_FALSE(object.remove("a"));
        CHECK(object.items.size() == 2);
    }

    TEST_CASE("finds nothing in a node that is not an object")
    {
        Node array = numbers({ "1" });
        CHECK(array.find("1") == nullptr);
        CHECK_FALSE(array.remove(""));
    }

    TEST_CASE("reads the value of a number")
    {
        CHECK(Node::makeNumber(std::string{ "2.5" }).number() == 2.5);
        CHECK(Node::makeNumber(0.1).text == "0.1");
        CHECK_FALSE(Node::makeString("2.5").number().has_value());
        CHECK_FALSE(Node::makeNumber(std::string{ "2.5x" }).number().has_value());
    }
}

TEST_SUITE("reflect::toNode")
{
    TEST_CASE("round-trips every field kind")
    {
        CHECK(std::get<bool>(roundTrip(FieldKind::Bool, true).second));
        CHECK(std::get<std::int64_t>(roundTrip(FieldKind::Int, std::int64_t{ -5'000'000'000 }).second) == -5'000'000'000);
        CHECK(std::get<std::uint64_t>(roundTrip(FieldKind::UInt, std::uint64_t{ 18'000'000'000'000'000'000ull }).second) == 18'000'000'000'000'000'000ull);
        CHECK(std::get<float>(roundTrip(FieldKind::Float, 0.1f).second) == 0.1f);
        CHECK(std::get<double>(roundTrip(FieldKind::Double, 0.1).second) == 0.1);
        CHECK(std::get<std::string>(roundTrip(FieldKind::String, std::string{ "a \"quoted\" text" }).second) == "a \"quoted\" text");
        const auto v2 = std::get<glm::vec2>(roundTrip(FieldKind::Vec2, glm::vec2{ 1.0f, 2.5f }).second);
        CHECK((v2.x == 1.0f && v2.y == 2.5f));
        const auto v3 = std::get<glm::vec3>(roundTrip(FieldKind::Vec3, glm::vec3{ 1.0f, 2.0f, 3.0f }).second);
        CHECK((v3.x == 1.0f && v3.y == 2.0f && v3.z == 3.0f));
        const auto v4 = std::get<glm::vec4>(roundTrip(FieldKind::Vec4, glm::vec4{ 1.0f, 2.0f, 3.0f, 4.0f }).second);
        CHECK((v4.x == 1.0f && v4.y == 2.0f && v4.z == 3.0f && v4.w == 4.0f));
        const auto q = std::get<glm::quat>(roundTrip(FieldKind::Quat, glm::quat{ 0.4f, 0.1f, 0.2f, 0.3f }).second);
        CHECK((q.x == 0.1f && q.y == 0.2f && q.z == 0.3f && q.w == 0.4f));
        CHECK(std::get<std::int64_t>(roundTrip(FieldKind::Enum, std::int64_t{ 3 }).second) == 3);
        CHECK(std::get<std::uint64_t>(roundTrip(FieldKind::AssetRef, std::uint64_t{ 0xa3f09c2e4b1d7788 }).second) == 0xa3f09c2e4b1d7788);
    }

    TEST_CASE("writes floats as their shortest text")
    {
        auto [node, value] = roundTrip(FieldKind::Float, 0.1f);
        CHECK(node.kind == Node::Kind::Number);
        CHECK(node.text == "0.1");
        CHECK(roundTrip(FieldKind::Vec3, glm::vec3{ 0.1f, -2.0f, 1e-7f }).first.items[2].text == "1e-07");
    }

    TEST_CASE("writes a quaternion as x y z w")
    {
        auto node = toNode(FieldKind::Quat, glm::quat{ 4.0f, 1.0f, 2.0f, 3.0f }); // glm takes w first
        REQUIRE(node.has_value());
        REQUIRE(node->items.size() == 4);
        CHECK(node->items[0].text == "1");
        CHECK(node->items[3].text == "4");
    }

    TEST_CASE("writes enums and asset references as strings")
    {
        CHECK(toNode(FieldKind::Enum, std::int64_t{ 3 }, modeEntries)->text == "On");
        auto asset = toNode(FieldKind::AssetRef, std::uint64_t{ 0x00000000000000ff });
        CHECK(asset->kind == Node::Kind::String);
        CHECK(asset->text == "00000000000000ff");
    }

    TEST_CASE("writes an empty asset reference as null")
    {
        auto node = toNode(FieldKind::AssetRef, std::uint64_t{ 0 });
        REQUIRE(node.has_value());
        CHECK(node->isNull());
        CHECK(std::get<std::uint64_t>(*fromNode(FieldKind::AssetRef, *node)) == 0);
    }

    TEST_CASE("rejects numbers JSON cannot hold")
    {
        CHECK(toNode(FieldKind::Float, std::numeric_limits<float>::infinity()).error().code == ErrorCode::OutOfRange);
        CHECK(toNode(FieldKind::Vec2, glm::vec2{ 0.0f, std::numeric_limits<float>::quiet_NaN() }).error().code == ErrorCode::OutOfRange);
    }
}

TEST_SUITE("reflect::fromNode")
{
    TEST_CASE("rejects a node of the wrong kind")
    {
        CHECK(fromNode(FieldKind::Bool, Node::makeString("true")).error().code == ErrorCode::TypeMismatch);
        CHECK(fromNode(FieldKind::Float, Node::makeString("1")).error().code == ErrorCode::TypeMismatch);
        CHECK(fromNode(FieldKind::String, Node::makeNumber(std::string{ "1" })).error().code == ErrorCode::TypeMismatch);
        CHECK(fromNode(FieldKind::Enum, Node{}).error().code == ErrorCode::TypeMismatch);
        CHECK(fromNode(FieldKind::AssetRef, Node::makeNumber(std::string{ "1" })).error().code == ErrorCode::TypeMismatch);
    }

    TEST_CASE("rejects an array of the wrong length or with non-numbers")
    {
        CHECK(fromNode(FieldKind::Vec3, numbers({ "1", "2" })).error().code == ErrorCode::TypeMismatch);
        CHECK(fromNode(FieldKind::Quat, numbers({ "1", "2", "3", "4", "5" })).error().code == ErrorCode::TypeMismatch);
        Node mixed = numbers({ "1" });
        mixed.push(Node::makeString("2"));
        CHECK(fromNode(FieldKind::Vec2, mixed).error().code == ErrorCode::TypeMismatch);
    }

    TEST_CASE("rejects a number the field cannot hold")
    {
        CHECK(fromNode(FieldKind::Int, Node::makeNumber(std::string{ "1.5" })).error().code == ErrorCode::ParseError);
        CHECK(fromNode(FieldKind::UInt, Node::makeNumber(std::string{ "-1" })).error().code == ErrorCode::ParseError);
        CHECK(fromNode(FieldKind::Float, Node::makeNumber(std::string{ "1e999" })).error().code == ErrorCode::OutOfRange);
    }

    TEST_CASE("reads exponents and integers into float fields")
    {
        CHECK(std::get<float>(*fromNode(FieldKind::Float, Node::makeNumber(std::string{ "100" }))) == 100.0f);
        CHECK(std::get<float>(*fromNode(FieldKind::Float, Node::makeNumber(std::string{ "1e+2" }))) == 100.0f);
        CHECK(std::get<double>(*fromNode(FieldKind::Double, Node::makeNumber(std::string{ "-2.5E-1" }))) == -0.25);
    }

    TEST_CASE("rejects an asset ID that is not 16 lowercase hex digits")
    {
        CHECK(fromNode(FieldKind::AssetRef, Node::makeString("")).error().code == ErrorCode::ParseError);
        CHECK(fromNode(FieldKind::AssetRef, Node::makeString("A3F09C2E4B1D7788")).error().code == ErrorCode::ParseError);
        CHECK(fromNode(FieldKind::AssetRef, Node::makeString("a3f0")).error().code == ErrorCode::ParseError);
    }

    TEST_CASE("rejects a name that is not an enumerator")
    {
        CHECK(fromNode(FieldKind::Enum, Node::makeString("Maybe"), modeEntries).error().code == ErrorCode::ParseError);
    }
}
