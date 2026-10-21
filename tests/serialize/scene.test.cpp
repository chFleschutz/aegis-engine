#include <doctest/doctest.h>

import std;
import aegis.math;
import aegis.reflect;
import aegis.scripting.script;
import aegis.serialize;
import aegis.world.registry;
import aegis.world.render_components;
import aegis.world.types;

namespace
{
using aegis::reflect::Node;
using aegis::reflect::TypeRegistry;
using aegis::serialize::ErrorCode;
using aegis::serialize::Scene;
using aegis::world::Entity;
using aegis::world::Registry;

enum class Armor
{
    None,
    Light,
    Heavy,
};

// A game's component, unknown to the engine
struct Health
{
    float value{ 100.0f };
    Armor armor{ Armor::None };
};

// A game's script
class Spinner : public aegis::scripting::Script
{
public:
    float speed{ 1.0f };
};
}

template<>
struct aegis::reflect::EnumNames<Armor>
{
    static constexpr auto values = std::to_array<std::pair<std::string_view, Armor>>({
        { "None", Armor::None },
        { "Light", Armor::Light },
        { "Heavy", Armor::Heavy },
    });
};

namespace
{
auto registerTypes(TypeRegistry& types) -> void
{
    REQUIRE(aegis::world::registerComponents(types).has_value());
    auto health = aegis::world::registerComponent<Health>(types, "Health");
    REQUIRE(health.has_value());
    REQUIRE(health->field("value", &Health::value).field("armor", &Health::armor).status().has_value());
    auto spinner = aegis::scripting::registerScript<Spinner>(types, "Spinner");
    REQUIRE(spinner.has_value());
    REQUIRE(spinner->field("speed", &Spinner::speed).status().has_value());
}

auto readFile(const std::filesystem::path& path) -> std::string
{
    std::ifstream file{ path, std::ios::binary };
    REQUIRE(file.good());
    return std::string{ std::istreambuf_iterator<char>{ file }, {} };
}

// The golden file without carriage returns, which a Windows checkout may add
auto golden() -> std::string
{
    std::string text = readFile(std::filesystem::path{ AEGIS_TEST_GOLDEN_DIR } / "scene.scene");
    std::erase(text, '\r');
    return text;
}

// A scene file around `entities` (the text of the entity list) that lists `types` in its header
auto sceneText(std::string_view entities, std::string_view types = R"("Health": 1, "Spinner": 1, "Transform": 1)") -> std::string
{
    return std::format(R"({{
  "type": "Scene",
  "id": "00000000000000aa",
  "version": 1,
  "types": {{ {} }},
  "entities": [
{}
  ]
}}
)",
        types, entities);
}

auto entityText(std::string_view id, std::string_view rest = {}) -> std::string
{
    return std::format(R"(    {{ "id": "{}", "name": "E"{} }},)", id, rest);
}

auto parse(const std::string& text, const TypeRegistry& types) -> std::expected<Scene, aegis::serialize::Error>
{
    return aegis::serialize::parseScene(text, types);
}

// A world with a hierarchy, built-in and game components, a tag and a script
auto buildWorld(Registry& registry, const TypeRegistry& types) -> void
{
    auto lamp = registry.createWithId(0x1c5e09b7f3a8d264, "Lamp", { 0.0f, 0.0f, 1.5f });
    registry.add<aegis::PointLight>(lamp, glm::vec3{ 1.0f, 0.5f, 0.25f }, 200.0f);
    auto bulb = registry.createWithId(0x7a21e4c90d3b5f86, "Spinning Bulb", { 0.0f, 0.0f, 0.25f });
    registry.add<aegis::DynamicTag>(bulb);
    auto crate = registry.createWithId(0x3b0f5e2a9c1d4e77, "Crate", { 2.0f, -1.0f, 0.0f });
    registry.add<Health>(crate, 75.0f, Armor::Heavy);
    registry.get<aegis::Transform>(crate).scale = glm::vec3{ 0.5f };
    registry.addChild(lamp, bulb);
    registry.addChild(lamp, crate);

    auto spinner = aegis::scripting::attachScript(registry, types, bulb, "Spinner");
    REQUIRE(spinner.has_value());
    static_cast<Spinner*>(*spinner)->speed = 2.5f;
}

auto saveText(Registry& registry, const TypeRegistry& types) -> std::string
{
    auto scene = aegis::serialize::saveScene(registry, types, 0x9d41c7e0a25b6f18);
    REQUIRE(scene.has_value());
    return aegis::serialize::writeScene(*scene);
}
}

TEST_SUITE("serialize::scene")
{
    TEST_CASE("saves, loads and saves a world to identical bytes")
    {
        TypeRegistry types;
        registerTypes(types);
        Registry original;
        buildWorld(original, types);
        const std::string first = saveText(original, types);

        auto scene = parse(first, types);
        REQUIRE(scene.has_value());
        Registry loaded;
        REQUIRE(aegis::serialize::instantiate(loaded, types, *scene).has_value());
        CHECK(saveText(loaded, types) == first);
    }

    TEST_CASE("restores names, values, tags, scripts and the sibling order")
    {
        TypeRegistry types;
        registerTypes(types);
        Registry original;
        buildWorld(original, types);
        auto scene = parse(saveText(original, types), types);
        REQUIRE(scene.has_value());
        Registry loaded;
        REQUIRE(aegis::serialize::instantiate(loaded, types, *scene).has_value());

        CHECK(loaded.entityCount() == 3);
        auto lamp = loaded.find(aegis::StableId{ 0x1c5e09b7f3a8d264 });
        auto bulb = loaded.find(aegis::StableId{ 0x7a21e4c90d3b5f86 });
        auto crate = loaded.find(aegis::StableId{ 0x3b0f5e2a9c1d4e77 });
        REQUIRE(lamp);
        REQUIRE(bulb);
        REQUIRE(crate);
        CHECK(loaded.get<aegis::Name>(lamp).name == "Lamp");
        CHECK(loaded.get<aegis::PointLight>(lamp).intensity == 200.0f);
        CHECK(loaded.get<aegis::PointLight>(lamp).color.y == 0.5f);
        CHECK(loaded.get<Health>(crate).armor == Armor::Heavy);
        CHECK(loaded.get<aegis::Transform>(crate).scale.x == 0.5f);
        CHECK(loaded.has<aegis::DynamicTag>(bulb));
        CHECK_FALSE(loaded.has<aegis::DynamicTag>(crate));

        CHECK(loaded.get<aegis::Children>(lamp).first == bulb); // Sibling order kept
        CHECK(loaded.get<aegis::Children>(lamp).last == crate);
        CHECK(loaded.get<aegis::Parent>(crate).entity == lamp);

        REQUIRE(loaded.has<aegis::scripting::Scripts>(bulb));
        const auto& entries = loaded.get<aegis::scripting::Scripts>(bulb).entries;
        REQUIRE(entries.size() == 1);
        CHECK(entries[0].name == "Spinner");
        CHECK(static_cast<Spinner*>(entries[0].script.get())->speed == 2.5f);
    }

    TEST_CASE("keeps loading the golden scene file unchanged")
    {
        TypeRegistry types;
        registerTypes(types);
        const std::string text = golden();
        auto scene = parse(text, types);
        REQUIRE(scene.has_value());
        CHECK(aegis::serialize::writeScene(*scene) == text);

        Registry registry;
        REQUIRE(aegis::serialize::instantiate(registry, types, *scene).has_value());
        CHECK(saveText(registry, types) == text);

        auto camera = registry.find(aegis::StableId{ 0xc0ffee0000000001 });
        REQUIRE(camera);
        CHECK(registry.has<aegis::MainCamera>(camera));
        CHECK(registry.get<aegis::Camera>(camera).far == 1000.0f);
        auto crate = registry.find(aegis::StableId{ 0x3b0f5e2a9c1d4e77 });
        CHECK(registry.get<aegis::MeshRenderer>(crate).mesh.id().value == 0x1f6b2c0d93ae5547);
        CHECK_FALSE(registry.get<aegis::MeshRenderer>(crate).material.id().valid());
    }

    TEST_CASE("writes entities sorted by ID")
    {
        TypeRegistry types;
        registerTypes(types);
        Registry registry;
        registry.createWithId(0x30, "C");
        registry.createWithId(0x10, "A");
        registry.createWithId(0x20, "B");
        const std::string text = saveText(registry, types);
        CHECK(text.find("\"A\"") < text.find("\"B\""));
        CHECK(text.find("\"B\"") < text.find("\"C\""));
    }

    TEST_CASE("accepts any entity order, comments, trailing commas and missing fields")
    {
        TypeRegistry types;
        registerTypes(types);
        const std::string text = sceneText(R"(
    // The child comes first
    { "id": "0000000000000002", "name": "Child", "components": { "Health": { "armor": "Light", }, }, },
    { "id": "0000000000000001", "name": "Parent", "children": ["0000000000000002"] },)");
        auto scene = parse(text, types);
        REQUIRE(scene.has_value());
        Registry registry;
        REQUIRE(aegis::serialize::instantiate(registry, types, *scene).has_value());
        auto child = registry.find(aegis::StableId{ 2 });
        CHECK(registry.get<Health>(child).armor == Armor::Light);
        CHECK(registry.get<Health>(child).value == 100.0f); // Missing: the default
        CHECK(registry.get<aegis::Parent>(child).entity == registry.find(aegis::StableId{ 1 }));
    }

    TEST_CASE("leaves the world unchanged when the scene does not fit it")
    {
        TypeRegistry types;
        registerTypes(types);
        Registry registry;
        registry.createWithId(2, "Existing");
        auto scene = parse(sceneText(entityText("0000000000000001") + "\n" + entityText("0000000000000002")), types);
        REQUIRE(scene.has_value());
        auto result = aegis::serialize::instantiate(registry, types, *scene);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::DuplicateEntity);
        CHECK(registry.entityCount() == 1);
    }

    TEST_CASE("undoes the entities it made when a value does not fit")
    {
        struct Small
        {
            std::int8_t value{ 0 };
        };
        TypeRegistry types;
        registerTypes(types);
        auto small = aegis::world::registerComponent<Small>(types, "Small");
        REQUIRE(small.has_value());
        REQUIRE(small->field("value", &Small::value).status().has_value());
        auto scene = parse(sceneText(entityText("0000000000000001") + "\n" + entityText("0000000000000002", R"(, "components": { "Small": { "value": 300 } })"),
                               R"("Small": 1)"),
            types);
        REQUIRE(scene.has_value());
        Registry registry;
        auto result = aegis::serialize::instantiate(registry, types, *scene);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::InvalidValue);
        CHECK(result.error().line == 8); // The second entity's
        CHECK(registry.entityCount() == 0);
    }
}

TEST_SUITE("serialize::scene versions")
{
    TEST_CASE("upgrades an old component version through its migrations")
    {
        // Health version 1 saved "hp" in hundreds; version 2 renamed it "value"; version 3 scaled it
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());
        auto health = aegis::world::registerComponent<Health>(types, "Health");
        REQUIRE(health.has_value());
        health->field("value", &Health::value)
            .version(3)
            .migration(1, [](Node& fields) -> std::expected<void, aegis::reflect::Error> {
                fields.rename("hp", "value");
                return {};
            })
            .migration(2, [](Node& fields) -> std::expected<void, aegis::reflect::Error> {
                if (const Node* value = fields.find("value"); value && value->number())
                    fields.set("value", Node::makeNumber(*value->number() * 100.0));
                return {};
            });
        REQUIRE(health->status().has_value());

        auto scene = parse(sceneText(entityText("0000000000000001", R"(, "components": { "Health": { "hp": 0.5 } })"), R"("Health": 1)"), types);
        REQUIRE(scene.has_value());
        Registry registry;
        REQUIRE(aegis::serialize::instantiate(registry, types, *scene).has_value());
        CHECK(registry.get<Health>(registry.find(aegis::StableId{ 1 })).value == 50.0f);

        const std::string saved = aegis::serialize::writeScene(*scene);
        CHECK(saved.contains(R"("Health": 3)"));
        CHECK(saved.contains(R"("value": 50)"));
    }

    TEST_CASE("rejects a component version newer than the engine")
    {
        TypeRegistry types;
        registerTypes(types);
        auto scene = parse(sceneText(entityText("0000000000000001", R"(, "components": { "Health": {} })"), R"("Health": 2)"), types);
        REQUIRE_FALSE(scene.has_value());
        CHECK(scene.error().code == ErrorCode::UnsupportedVersion);
        CHECK(scene.error().line == 7);
    }

    TEST_CASE("rejects a scene layout version newer than the engine")
    {
        TypeRegistry types;
        registerTypes(types);
        std::string text = sceneText(entityText("0000000000000001"));
        text.replace(text.find(R"("version": 1)"), 12, R"("version": 2)");
        auto scene = parse(text, types);
        REQUIRE_FALSE(scene.has_value());
        CHECK(scene.error().code == ErrorCode::UnsupportedVersion);
        CHECK(scene.error().line == 4);
    }

    TEST_CASE("reads earlier type and field names and writes the current ones")
    {
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());
        auto health = aegis::world::registerComponent<Health>(types, "Health");
        REQUIRE(health.has_value());
        REQUIRE(health->field("value", &Health::value).alias("Hitpoints").fieldAlias("amount", "value").status().has_value());

        auto scene = parse(sceneText(entityText("0000000000000001", R"(, "components": { "Hitpoints": { "amount": 5 } })"), R"("Hitpoints": 1)"), types);
        REQUIRE(scene.has_value());
        const std::string saved = aegis::serialize::writeScene(*scene);
        CHECK(saved.contains(R"("Health": {)"));
        CHECK(saved.contains(R"("value": 5)"));
        CHECK_FALSE(saved.contains("Hitpoints"));
        CHECK_FALSE(saved.contains("amount"));
    }
}

TEST_SUITE("serialize::scene errors")
{
    // Each case's error is on line 7, the first entity's line (see sceneText).
    struct Case
    {
        std::string_view name;
        std::string entities;
        ErrorCode code;
        std::uint32_t line;
        std::string types{ R"("Health": 1, "Spinner": 1, "Transform": 1)" };
    };

    TEST_CASE("fails with the line of the problem")
    {
        const auto cases = std::to_array<Case>({
            { "an unknown component type", entityText("0000000000000001", R"(, "components": { "Mana": {} })"), ErrorCode::UnknownType, 7 },
            { "an unknown script type", entityText("0000000000000001", R"(, "scripts": [{ "Jumper": {} }])"), ErrorCode::UnknownType, 7 },
            { "an unknown field", entityText("0000000000000001", R"(, "components": { "Health": { "mana": 1 } })"), ErrorCode::UnknownField, 7 },
            { "a value of the wrong kind", entityText("0000000000000001", R"(, "components": { "Health": { "value": "full" } })"), ErrorCode::InvalidValue, 7 },
            { "an enum value that is no enumerator", entityText("0000000000000001", R"(, "components": { "Health": { "armor": "Plate" } })"), ErrorCode::InvalidValue, 7 },
            { "a component missing from the types table", entityText("0000000000000001", R"(, "components": { "PointLight": {} })"), ErrorCode::InvalidData, 7 },
            { "a script listed as a component", entityText("0000000000000001", R"(, "components": { "Spinner": {} })"), ErrorCode::InvalidData, 7 },
            { "an unknown entity key", entityText("0000000000000001", R"(, "parent": "0000000000000002")"), ErrorCode::InvalidData, 7 },
            { "a bad entity ID", entityText("00000000000000XY"), ErrorCode::InvalidData, 7 },
            { "a zero entity ID", entityText("0000000000000000"), ErrorCode::InvalidData, 7 },
            { "an entity listed twice", entityText("0000000000000001") + "\n" + entityText("0000000000000001"), ErrorCode::DuplicateEntity, 8 },
            { "a missing child", entityText("0000000000000001", R"(, "children": ["0000000000000002"])"), ErrorCode::MissingEntity, 7 },
            { "a child listed twice", entityText("0000000000000001", R"(, "children": ["0000000000000002"])") + "\n" + entityText("0000000000000003", R"(, "children": ["0000000000000002"])") + "\n" + entityText("0000000000000002"), ErrorCode::DuplicateEntity, 8 },
            { "an entity that is its own child", entityText("0000000000000001", R"(, "children": ["0000000000000001"])"), ErrorCode::Cycle, 7 },
            { "a cycle of two", entityText("0000000000000001", R"(, "children": ["0000000000000002"])") + "\n" + entityText("0000000000000002", R"(, "children": ["0000000000000001"])"), ErrorCode::Cycle, 7 },
            { "an unknown type in the types table", entityText("0000000000000001"), ErrorCode::UnknownType, 5, R"("Mana": 1)" },
        });

        TypeRegistry types;
        registerTypes(types);
        for (const auto& c : cases)
        {
            CAPTURE(c.name);
            auto scene = parse(sceneText(c.entities, c.types), types);
            REQUIRE_FALSE(scene.has_value());
            CAPTURE(aegis::serialize::describe(scene.error()));
            CHECK(scene.error().code == c.code);
            CHECK(scene.error().line == c.line);
        }
    }

    TEST_CASE("names the line in the description")
    {
        TypeRegistry types;
        registerTypes(types);
        auto scene = parse(sceneText(entityText("0000000000000001", R"(, "components": { "Mana": {} })")), types);
        REQUIRE_FALSE(scene.has_value());
        CHECK(aegis::serialize::describe(scene.error()).starts_with("line 7: UnknownType"));
    }

    TEST_CASE("rejects a file of another type")
    {
        TypeRegistry types;
        registerTypes(types);
        auto scene = parse(R"({ "type": "Material", "id": "00000000000000aa", "version": 1 })", types);
        REQUIRE_FALSE(scene.has_value());
        CHECK(scene.error().code == ErrorCode::WrongType);
    }

    TEST_CASE("rejects a scene without its header")
    {
        TypeRegistry types;
        registerTypes(types);
        CHECK(parse(R"({ "type": "Scene", "id": "00000000000000aa", "version": 1, "entities": [] })", types).error().code == ErrorCode::InvalidData);
        CHECK(parse(R"({ "type": "Scene", "version": 1, "types": {}, "entities": [] })", types).error().code == ErrorCode::InvalidData);
        CHECK(parse(R"([])", types).error().code == ErrorCode::InvalidData);
    }
}
