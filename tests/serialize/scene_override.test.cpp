#include <doctest/doctest.h>

import std;
import aegis.assets;
import aegis.core;
import aegis.math;
import aegis.reflect;
import aegis.serialize;
import aegis.world;

namespace
{
namespace log = aegis::log;
using aegis::PointLight;
using aegis::StableId;
using aegis::assets::AssetId;
using aegis::assets::AssetRef;
using aegis::reflect::TypeRegistry;
using aegis::serialize::deriveEntityId;
using aegis::serialize::ErrorCode;
using aegis::serialize::InstanceMember;
using aegis::serialize::InstanceOverride;
using aegis::serialize::Scene;
using aegis::serialize::SceneInstance;
using aegis::world::Entity;
using aegis::world::Registry;

constexpr std::uint64_t levelId = 0x00000000000000aa;
constexpr std::uint64_t propId = 0x00000000000000bb;
constexpr std::uint64_t lampId = 0x00000000000000cc;
constexpr std::uint64_t holderId = 0x0000000000000010;

auto registerTypes(TypeRegistry& types) -> void
{
    REQUIRE(aegis::world::registerComponents(types).has_value());
    REQUIRE(aegis::serialize::registerComponents(types).has_value());
}

auto sceneText(std::uint64_t id, std::string_view entities) -> std::string
{
    return std::format(R"({{
  "type": "Scene",
  "id": "{:016x}",
  "version": 1,
  "types": {{ "PointLight": 1, "SceneInstance": 1, "Transform": 1 }},
  "entities": [
{}
  ]
}}
)",
        id, entities);
}

// A lamp: an entity with a PointLight of 'intensity' and 'range'
auto lampText(std::uint64_t id, float intensity, float range = 20.0f) -> std::string
{
    return std::format(R"(    {{ "id": "{:016x}", "name": "Lamp", "components": {{ "PointLight": {{ "intensity": {}, "range": {} }} }} }},)", id, intensity, range);
}

// An entity holding an instance of 'scene', with the text of its "overrides" object if given
auto holderText(std::uint64_t id, std::uint64_t scene, std::string_view overrides = {}) -> std::string
{
    return std::format(R"(    {{ "id": "{:016x}", "name": "Holder", "components": {{ "SceneInstance": {{ "scene": "{:016x}" }} }}{}{} }},)", id, scene,
        overrides.empty() ? "" : R"(, "overrides": )", overrides);
}

// The scene files of a test, which 'prepare' hands to the instances naming them, as the asset library does
class Scenes
{
public:
    explicit Scenes(const TypeRegistry& types) :
        m_types{ types }
    {
    }

    auto add(std::uint64_t id, std::string_view entities) -> const Scene&
    {
        auto scene = aegis::serialize::parseScene(sceneText(id, entities), m_types);
        REQUIRE(scene.has_value());
        auto shared = std::make_shared<Scene>(std::move(*scene));
        m_refs.insert_or_assign(id, AssetRef<Scene>::ready(AssetId{ id }, shared));
        return *shared;
    }

    auto prepare() const -> aegis::serialize::PrepareComponent
    {
        return [this](const aegis::reflect::TypeInfo& type, void* component) {
            if (type.type != typeid(SceneInstance))
                return;
            auto& instance = *static_cast<SceneInstance*>(component);
            if (auto it = m_refs.find(instance.scene.id().value); it != m_refs.end())
                instance.scene = it->second;
        };
    }

private:
    const TypeRegistry& m_types;
    std::map<std::uint64_t, AssetRef<Scene>> m_refs;
};

auto entity(Registry& registry, std::uint64_t id) -> Entity
{
    const Entity found = registry.find(StableId{ id });
    REQUIRE(found);
    return found;
}

auto lampIntensity(Registry& registry, std::uint64_t holder, std::uint64_t localId) -> float
{
    return registry.get<PointLight>(entity(registry, deriveEntityId(holder, localId))).intensity;
}

class LogCapture
{
public:
    LogCapture()
    {
        log::setSink([this](log::Level level, std::string_view message) {
            if (level == log::Level::Warn)
                warnings.emplace_back(message);
        });
    }
    ~LogCapture() { log::setSink({}); }
    LogCapture(const LogCapture&) = delete;
    auto operator=(const LogCapture&) -> LogCapture& = delete;

    std::vector<std::string> warnings;
};
}

TEST_SUITE("serialize::scene overrides")
{
    TEST_CASE("reads and writes the overrides of a holder")
    {
        TypeRegistry types;
        registerTypes(types);
        const auto text = sceneText(levelId, holderText(holderId, propId, R"({ "0000000000000002": { "PointLight": { "range": 5, "intensity": 300 } } })"));
        auto scene = aegis::serialize::parseScene(text, types);
        REQUIRE(scene.has_value());
        REQUIRE(scene->entities.size() == 1);
        const auto& overrides = scene->entities.front().overrides;
        REQUIRE(overrides.size() == 1);
        CHECK(overrides.front().entity == 2);
        REQUIRE(overrides.front().components.size() == 1);
        CHECK(overrides.front().components.front().type->name == "PointLight");

        // Written in the fields' registration order, with the type in the header; the same bytes again
        const auto written = aegis::serialize::writeScene(*scene);
        CHECK(written.contains(R"("overrides": {)"));
        CHECK(written.contains(R"("0000000000000002": {)"));
        CHECK(written.find(R"("intensity": 300)") < written.find(R"("range": 5)"));
        CHECK(written.contains(R"("PointLight": 1)"));
        auto again = aegis::serialize::parseScene(written, types);
        REQUIRE(again.has_value());
        CHECK(aegis::serialize::writeScene(*again) == written);
    }

    // The file format is fixed: this pins the layout of an entity with overrides
    TEST_CASE("writes overrides after the components, by entity and component")
    {
        TypeRegistry types;
        registerTypes(types);
        auto scene = aegis::serialize::parseScene(
            sceneText(levelId, holderText(holderId, propId, R"({ "0000000000000003": { "PointLight": { "range": 5 } }, "0000000000000002": { "Transform": { "scale": [2, 2, 2] }, "PointLight": { "intensity": 300 } } })")),
            types);
        REQUIRE(scene.has_value());
        const auto written = aegis::serialize::writeScene(*scene);
        const auto start = written.find(R"("overrides")");
        REQUIRE(start != std::string::npos);
        CHECK(written.substr(start) == R"("overrides": {
        "0000000000000002": {
          "PointLight": {
            "intensity": 300
          },
          "Transform": {
            "scale": [2, 2, 2]
          }
        },
        "0000000000000003": {
          "PointLight": {
            "range": 5
          }
        }
      }
    }
  ]
}
)");
    }

    TEST_CASE("leaves out the key when a holder has no overrides")
    {
        TypeRegistry types;
        registerTypes(types);
        auto scene = aegis::serialize::parseScene(sceneText(levelId, holderText(holderId, propId)), types);
        REQUIRE(scene.has_value());
        CHECK_FALSE(aegis::serialize::writeScene(*scene).contains("overrides"));
    }

    TEST_CASE("rejects overrides that do not fit")
    {
        TypeRegistry types;
        registerTypes(types);
        struct Case
        {
            std::string entities;
            ErrorCode code;
        };
        const auto cases = std::to_array<Case>({
            { R"(    { "id": "0000000000000010", "overrides": { "0000000000000002": { "PointLight": { "intensity": 1 } } } },)", ErrorCode::InvalidData },
            { holderText(holderId, propId, R"({ "2": { "PointLight": { "intensity": 1 } } })"), ErrorCode::InvalidData },
            { holderText(holderId, propId, R"({ "0000000000000002": { "Nope": { "intensity": 1 } } })"), ErrorCode::UnknownType },
            { holderText(holderId, propId, R"({ "0000000000000002": { "PointLight": { "nope": 1 } } })"), ErrorCode::UnknownField },
            { holderText(holderId, propId, R"({ "0000000000000002": { "PointLight": { "intensity": "bright" } } })"), ErrorCode::InvalidValue },
            { holderText(holderId, propId, R"({ "0000000000000002": [] })"), ErrorCode::InvalidData },
            { holderText(holderId, propId, R"({ "0000000000000005": { "SceneInstance": { "scene": null } } })"), ErrorCode::InvalidData },
        });
        for (const auto& [entities, code] : cases)
        {
            CAPTURE(entities);
            auto scene = aegis::serialize::parseScene(sceneText(levelId, entities), types);
            REQUIRE_FALSE(scene.has_value());
            CHECK(scene.error().code == code);
        }
    }

    TEST_CASE("writes the overrides over the instance's scene")
    {
        TypeRegistry types;
        registerTypes(types);
        Scenes scenes{ types };
        scenes.add(propId, lampText(1, 100.0f) + "\n" + lampText(2, 100.0f, 7.0f));
        const Scene& level = scenes.add(levelId, holderText(holderId, propId, R"({ "0000000000000002": { "PointLight": { "intensity": 300 } } })"));

        Registry registry;
        REQUIRE(aegis::serialize::instantiate(registry, types, level, scenes.prepare()).has_value());
        CHECK(lampIntensity(registry, holderId, 1) == 100.0f);
        CHECK(lampIntensity(registry, holderId, 2) == 300.0f);
        CHECK(registry.get<PointLight>(entity(registry, deriveEntityId(holderId, 2))).range == 7.0f); // Not overridden

        const auto& instance = registry.get<SceneInstance>(entity(registry, holderId));
        REQUIRE(instance.overrides.size() == 1);
        CHECK(instance.overrides.front() == InstanceOverride{ .entity = 2, .component = "PointLight", .field = "intensity", .value = 300.0f });
        CHECK(registry.get<InstanceMember>(entity(registry, deriveEntityId(holderId, 2))).localId == 2);
    }

    TEST_CASE("keeps an override when the scene changes another field")
    {
        TypeRegistry types;
        registerTypes(types);
        Scenes scenes{ types };
        const auto overrides = R"({ "0000000000000002": { "PointLight": { "intensity": 300 } } })";
        scenes.add(propId, lampText(2, 100.0f, 7.0f));
        const Scene level = scenes.add(levelId, holderText(holderId, propId, overrides));

        // The prop's file is edited: another field of the overridden component, and the overridden field's own value
        scenes.add(propId, lampText(2, 50.0f, 9.0f));
        Registry registry;
        REQUIRE(aegis::serialize::instantiate(registry, types, level, scenes.prepare()).has_value());
        const auto& light = registry.get<PointLight>(entity(registry, deriveEntityId(holderId, 2)));
        CHECK(light.intensity == 300.0f);
        CHECK(light.range == 9.0f);
    }

    TEST_CASE("saves a holder's overrides and loads them back")
    {
        TypeRegistry types;
        registerTypes(types);
        Scenes scenes{ types };
        scenes.add(propId, lampText(2, 100.0f));
        const Scene& level = scenes.add(levelId, holderText(holderId, propId));

        Registry registry;
        REQUIRE(aegis::serialize::instantiate(registry, types, level, scenes.prepare()).has_value());
        registry.get<SceneInstance>(entity(registry, holderId)).setOverride({ .entity = 2, .component = "PointLight", .field = "intensity", .value = 250.0f });

        auto saved = aegis::serialize::saveScene(registry, types, levelId);
        REQUIRE(saved.has_value());
        auto read = aegis::serialize::parseScene(aegis::serialize::writeScene(*saved), types);
        REQUIRE(read.has_value());
        Registry loaded;
        REQUIRE(aegis::serialize::instantiate(loaded, types, *read, scenes.prepare()).has_value());
        CHECK(lampIntensity(loaded, holderId, 2) == 250.0f);
    }

    TEST_CASE("keeps an override that no longer applies, with a warning")
    {
        TypeRegistry types;
        registerTypes(types);
        Scenes scenes{ types };
        scenes.add(propId, lampText(1, 100.0f));
        // The prop has no entity 2 (any more): only that override is stale
        const Scene& level = scenes.add(levelId, holderText(holderId, propId, R"({ "0000000000000002": { "PointLight": { "intensity": 300 } }, "0000000000000001": { "PointLight": { "intensity": 200 } } })"));

        LogCapture capture;
        Registry registry;
        REQUIRE(aegis::serialize::instantiate(registry, types, level, scenes.prepare()).has_value());
        CHECK(lampIntensity(registry, holderId, 1) == 200.0f);
        REQUIRE(capture.warnings.size() == 1);
        CHECK(capture.warnings.front().contains("PointLight.intensity on entity 0000000000000002"));
        CHECK(registry.get<SceneInstance>(entity(registry, holderId)).overrides.size() == 2);
    }

    TEST_CASE("applies overrides of nested holders from their own scene")
    {
        TypeRegistry types;
        registerTypes(types);
        Scenes scenes{ types };
        scenes.add(lampId, lampText(1, 100.0f));
        // The prop holds a lamp with its own override; the level overrides the prop's holder's Transform
        scenes.add(propId, holderText(5, lampId, R"({ "0000000000000001": { "PointLight": { "intensity": 400 } } })"));
        const Scene& level = scenes.add(levelId, holderText(holderId, propId, R"({ "0000000000000005": { "Transform": { "location": [1, 2, 3] } } })"));

        Registry registry;
        REQUIRE(aegis::serialize::instantiate(registry, types, level, scenes.prepare()).has_value());
        const std::uint64_t nested = deriveEntityId(holderId, 5);
        CHECK(registry.get<aegis::Transform>(entity(registry, nested)).location == aegis::math::Float3{ 1.0f, 2.0f, 3.0f });
        CHECK(lampIntensity(registry, nested, 1) == 400.0f);
    }
}

TEST_SUITE("serialize::SceneInstance overrides")
{
    TEST_CASE("sets, replaces, finds and removes an override, sorted")
    {
        SceneInstance instance;
        instance.setOverride({ .entity = 2, .component = "PointLight", .field = "range", .value = 1.0f });
        instance.setOverride({ .entity = 1, .component = "Transform", .field = "scale", .value = 1.0f });
        instance.setOverride({ .entity = 2, .component = "PointLight", .field = "intensity", .value = 2.0f });
        instance.setOverride({ .entity = 2, .component = "PointLight", .field = "range", .value = 3.0f });
        REQUIRE(instance.overrides.size() == 3);
        CHECK(instance.overrides[0].entity == 1);
        CHECK(instance.overrides[1].field == "intensity");
        CHECK(instance.overrides[2].field == "range");

        const auto* found = instance.findOverride(2, "PointLight", "range");
        REQUIRE(found);
        CHECK(found->value == aegis::reflect::Value{ 3.0f });
        CHECK(instance.removeOverride(2, "PointLight", "range"));
        CHECK_FALSE(instance.removeOverride(2, "PointLight", "range"));
        CHECK_FALSE(instance.findOverride(2, "PointLight", "range"));
    }
}

TEST_SUITE("serialize::sceneValue")
{
    TEST_CASE("gives the file's value, or the default for a field or component the file leaves out")
    {
        TypeRegistry types;
        registerTypes(types);
        Scenes scenes{ types };
        const Scene& prop = scenes.add(propId, lampText(2, 150.0f));
        const auto* type = types.find("PointLight");
        REQUIRE(type);

        auto intensity = aegis::serialize::sceneValue(prop, 2, *type, *type->findField("intensity"));
        REQUIRE(intensity.has_value());
        CHECK(*intensity == aegis::reflect::Value{ 150.0f });
        auto color = aegis::serialize::sceneValue(prop, 2, *type, *type->findField("color"));
        REQUIRE(color.has_value());
        CHECK(*color == aegis::reflect::Value{ PointLight{}.color });

        // The entity's Transform, which the file leaves out
        const auto* transform = types.find("Transform");
        REQUIRE(transform);
        auto scale = aegis::serialize::sceneValue(prop, 2, *transform, *transform->findField("scale"));
        REQUIRE(scale.has_value());
        CHECK(*scale == aegis::reflect::Value{ aegis::Transform{}.scale });

        CHECK(aegis::serialize::sceneValue(prop, 3, *type, *type->findField("color")).error().code == ErrorCode::MissingEntity);
    }
}

TEST_SUITE("serialize::holdsScene")
{
    TEST_CASE("finds a scene held directly or through nested instances")
    {
        TypeRegistry types;
        registerTypes(types);
        Scenes scenes{ types };
        scenes.add(lampId, lampText(1, 100.0f));
        scenes.add(propId, holderText(5, lampId));
        const Scene& level = scenes.add(levelId, holderText(holderId, propId) + "\n" + lampText(0x20, 1.0f));

        Registry registry;
        REQUIRE(aegis::serialize::instantiate(registry, types, level, scenes.prepare()).has_value());
        CHECK(aegis::serialize::holdsScene(registry, propId, entity(registry, holderId)));
        CHECK(aegis::serialize::holdsScene(registry, lampId, entity(registry, holderId)));
        CHECK_FALSE(aegis::serialize::holdsScene(registry, levelId, entity(registry, holderId)));
        CHECK_FALSE(aegis::serialize::holdsScene(registry, lampId, entity(registry, 0x20)));
    }
}
