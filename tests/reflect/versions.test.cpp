#include <doctest/doctest.h>

import std;
import aegis.reflect;

namespace
{
using namespace aegis::reflect;

struct Rotator
{
    float speed{ 90.0f };
};

struct Light
{
    float intensity{ 1.0f };
    float range{ 10.0f };
};

struct Storage
{
    int marker{ 0 };
};

auto none(Node&) -> std::expected<void, Error>
{
    return {};
}

// Version 1 of Rotator saved "degrees"; version 2 renamed it "speed"; version 3 halved it.
auto registerVersioned(TypeRegistry& types) -> void
{
    auto builder = types.component<Rotator>("Rotator");
    REQUIRE(builder.has_value());
    builder->field("speed", &Rotator::speed)
        .version(3)
        .migration(1, [](Node& fields) -> std::expected<void, Error> {
            fields.rename("degrees", "speed");
            return {};
        })
        .migration(2, [](Node& fields) -> std::expected<void, Error> {
            const Node* speed = fields.find("speed");
            if (!speed || !speed->number())
                return makeError(ErrorCode::ParseError, "speed is not a number");
            fields.set("speed", Node::makeNumber(*speed->number() / 2.0));
            return {};
        });
    REQUIRE(builder->status().has_value());
}

auto speedOf(const Node& fields) -> std::optional<double>
{
    const Node* speed = fields.find("speed");
    return speed ? speed->number() : std::nullopt;
}
}

TEST_SUITE("reflect::TypeInfo::upgrade")
{
    TEST_CASE("starts every type at version 1")
    {
        TypeRegistry types;
        REQUIRE(types.component<Rotator>("Rotator").has_value());
        CHECK(types.find<Rotator>()->version == 1);
    }

    TEST_CASE("upgrades through the migration chain")
    {
        TypeRegistry types;
        registerVersioned(types);
        Node fields = Node::makeObject();
        fields.add("degrees", Node::makeNumber(std::string{ "90" }));

        REQUIRE(types.find<Rotator>()->upgrade(fields, 1).has_value());
        CHECK(fields.find("degrees") == nullptr);
        CHECK(speedOf(fields) == 45.0);
    }

    TEST_CASE("runs only the steps after the saved version")
    {
        TypeRegistry types;
        registerVersioned(types);
        Node fields = Node::makeObject();
        fields.add("speed", Node::makeNumber(std::string{ "90" }));

        REQUIRE(types.find<Rotator>()->upgrade(fields, 2).has_value());
        CHECK(speedOf(fields) == 45.0);
        REQUIRE(types.find<Rotator>()->upgrade(fields, 3).has_value());
        CHECK(speedOf(fields) == 45.0);
    }

    TEST_CASE("rejects a version newer than the type or zero")
    {
        TypeRegistry types;
        registerVersioned(types);
        Node fields = Node::makeObject();
        CHECK(types.find<Rotator>()->upgrade(fields, 4).error().code == ErrorCode::UnsupportedVersion);
        CHECK(types.find<Rotator>()->upgrade(fields, 0).error().code == ErrorCode::UnsupportedVersion);
    }

    TEST_CASE("fails on a missing migration step")
    {
        TypeRegistry types;
        auto builder = types.component<Rotator>("Rotator");
        REQUIRE(builder.has_value());
        builder->version(3).migration(2, none);
        REQUIRE(builder->status().has_value());
        Node fields = Node::makeObject();
        CHECK(types.find<Rotator>()->upgrade(fields, 1).error().code == ErrorCode::UnsupportedVersion);
        CHECK(types.find<Rotator>()->upgrade(fields, 2).has_value());
    }

    TEST_CASE("returns a failing migration's error")
    {
        TypeRegistry types;
        registerVersioned(types);
        Node fields = Node::makeObject();
        fields.add("speed", Node::makeString("fast"));
        CHECK(types.find<Rotator>()->upgrade(fields, 2).error().code == ErrorCode::ParseError);
    }
}

TEST_SUITE("reflect::TypeBuilder::version")
{
    TEST_CASE("rejects version 0")
    {
        TypeRegistry types;
        auto builder = types.component<Rotator>("Rotator");
        REQUIRE(builder.has_value());
        CHECK(builder->version(0).status().error().code == ErrorCode::InvalidVersion);
        CHECK(builder->info().version == 1);
    }

    TEST_CASE("rejects a migration from the current version or later")
    {
        TypeRegistry types;
        auto builder = types.component<Rotator>("Rotator");
        REQUIRE(builder.has_value());
        CHECK(builder->migration(1, none).status().error().code == ErrorCode::InvalidVersion); // Still at version 1
        CHECK(builder->info().migrations.empty());
    }

    TEST_CASE("rejects a migration from version 0")
    {
        TypeRegistry types;
        auto builder = types.component<Rotator>("Rotator");
        REQUIRE(builder.has_value());
        CHECK(builder->version(2).migration(0, none).status().error().code == ErrorCode::InvalidVersion);
    }

    TEST_CASE("rejects a second migration from the same version")
    {
        TypeRegistry types;
        auto builder = types.component<Rotator>("Rotator");
        REQUIRE(builder.has_value());
        CHECK(builder->version(2).migration(1, none).migration(1, none).status().error().code == ErrorCode::InvalidVersion);
    }

    TEST_CASE("rejects lowering the version below a migration")
    {
        TypeRegistry types;
        auto builder = types.component<Rotator>("Rotator");
        REQUIRE(builder.has_value());
        CHECK(builder->version(3).migration(2, none).version(2).status().error().code == ErrorCode::InvalidVersion);
        CHECK(builder->info().version == 3);
    }
}

TEST_SUITE("reflect::TypeBuilder::alias")
{
    TEST_CASE("finds a type by an earlier name")
    {
        TypeRegistry types;
        auto builder = types.component<Rotator>("Rotator");
        REQUIRE(builder.has_value());
        builder->alias("Spinner").alias("Turner");
        REQUIRE(builder->status().has_value());

        CHECK(types.find("Spinner") == types.find<Rotator>());
        CHECK(types.find("Turner") == types.find<Rotator>());
        CHECK(types.find<Rotator>()->name == "Rotator");
        CHECK(types.find<Rotator>()->aliases.size() == 2);
        CHECK(types.types().size() == 1);
    }

    TEST_CASE("rejects an alias that names another type")
    {
        TypeRegistry types;
        REQUIRE(types.component<Light>("Light").has_value());
        auto builder = types.component<Rotator>("Rotator");
        REQUIRE(builder.has_value());
        CHECK(builder->alias("Light").status().error().code == ErrorCode::DuplicateName);
        CHECK(types.find("Light") == types.find<Light>());
    }

    TEST_CASE("rejects an empty alias")
    {
        TypeRegistry types;
        auto builder = types.component<Rotator>("Rotator");
        REQUIRE(builder.has_value());
        CHECK(builder->alias("").status().error().code == ErrorCode::InvalidName);
    }

    TEST_CASE("keeps a later type from taking an alias")
    {
        TypeRegistry types;
        auto builder = types.component<Rotator>("Rotator");
        REQUIRE(builder.has_value());
        REQUIRE(builder->alias("Spinner").status().has_value());
        CHECK(types.component<Light>("Spinner").error().code == ErrorCode::DuplicateName);
    }
}

TEST_SUITE("reflect::TypeBuilder::fieldAlias")
{
    TEST_CASE("finds a field by an earlier name")
    {
        TypeRegistry types;
        auto builder = types.component<Rotator>("Rotator");
        REQUIRE(builder.has_value());
        builder->field("speed", &Rotator::speed).fieldAlias("degrees", "speed");
        REQUIRE(builder->status().has_value());

        const TypeInfo* info = types.find<Rotator>();
        REQUIRE(info->findField("degrees") != nullptr);
        CHECK(info->findField("degrees")->name == "speed");
        Rotator object;
        REQUIRE(info->set(&object, "degrees", 5.0f).has_value());
        CHECK(object.speed == 5.0f);
        CHECK(info->fields.size() == 1);
    }

    TEST_CASE("rejects an alias of an unknown field")
    {
        TypeRegistry types;
        auto builder = types.component<Light>("Light");
        REQUIRE(builder.has_value());
        CHECK(builder->field("intensity", &Light::intensity).fieldAlias("old", "missing").status().error().code == ErrorCode::UnknownField);
    }

    TEST_CASE("rejects an alias named like a field")
    {
        TypeRegistry types;
        auto builder = types.component<Light>("Light");
        REQUIRE(builder.has_value());
        builder->field("intensity", &Light::intensity).field("range", &Light::range).fieldAlias("range", "intensity");
        CHECK(builder->status().error().code == ErrorCode::DuplicateName);
        CHECK(types.find<Light>()->findField("range")->name == "range");
    }

    TEST_CASE("rejects a field named like an alias")
    {
        TypeRegistry types;
        auto builder = types.component<Light>("Light");
        REQUIRE(builder.has_value());
        builder->field("intensity", &Light::intensity).fieldAlias("power", "intensity").field("power", &Light::range);
        CHECK(builder->status().error().code == ErrorCode::DuplicateName);
        CHECK(builder->info().fields.size() == 1);
    }
}

TEST_SUITE("reflect::TypeBuilder::attach")
{
    TEST_CASE("keeps data attached to a type")
    {
        TypeRegistry types;
        auto builder = types.component<Rotator>("Rotator");
        REQUIRE(builder.has_value());
        builder->attach(Storage{ .marker = 7 });

        const TypeInfo* info = types.find<Rotator>();
        REQUIRE(info->attachment<Storage>() != nullptr);
        CHECK(info->attachment<Storage>()->marker == 7);
        CHECK(info->attachment<int>() == nullptr);
    }

    TEST_CASE("replaces data attached again")
    {
        TypeRegistry types;
        auto builder = types.component<Rotator>("Rotator");
        REQUIRE(builder.has_value());
        builder->attach(Storage{ .marker = 1 }).attach(Storage{ .marker = 2 });
        CHECK(types.find<Rotator>()->attachment<Storage>()->marker == 2);
    }
}
