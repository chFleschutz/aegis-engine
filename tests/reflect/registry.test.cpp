#include <doctest/doctest.h>

import std;
import aegis.math;
import aegis.reflect;

namespace
{
using namespace aegis::reflect;

enum class Mode : std::uint8_t
{
    Off,
    On = 3,
};

enum Plain
{
    Low = -1,
    High = 7,
};

// A component with every supported field type.
struct Everything
{
    bool flag{ false };
    std::int8_t small{ 0 };
    std::int32_t integer{ 0 };
    std::int64_t big{ 0 };
    std::uint16_t unsignedSmall{ 0 };
    std::uint32_t unsignedInt{ 0 };
    std::uint64_t unsignedBig{ 0 };
    float real{ 0.0f };
    double precise{ 0.0 };
    std::string text;
    glm::vec2 v2{ 0.0f };
    glm::vec3 v3{ 0.0f };
    glm::vec4 v4{ 0.0f };
    glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
    Mode mode{ Mode::Off };
    Plain plain{ Low };
};

struct Base
{
    int baseValue{ 1 };
};

struct Derived : Base
{
    float own{ 2.0f };
};

struct Rotator
{
    float speed{ 90.0f };
};

struct Unregistered
{
};
}

template<>
struct aegis::reflect::EnumNames<Mode>
{
    static constexpr auto values = std::to_array<std::pair<std::string_view, Mode>>({ { "Off", Mode::Off }, { "On", Mode::On } });
};

template<>
struct aegis::reflect::EnumNames<Plain>
{
    static constexpr auto values = std::to_array<std::pair<std::string_view, Plain>>({ { "Low", Low }, { "High", High } });
};

namespace
{
auto registerEverything(TypeRegistry& types) -> void
{
    auto builder = types.component<Everything>("Everything");
    REQUIRE(builder.has_value());
    builder->field("flag", &Everything::flag)
        .field("small", &Everything::small)
        .field("integer", &Everything::integer)
        .field("big", &Everything::big)
        .field("unsignedSmall", &Everything::unsignedSmall)
        .field("unsignedInt", &Everything::unsignedInt)
        .field("unsignedBig", &Everything::unsignedBig)
        .field("real", &Everything::real)
        .field("precise", &Everything::precise)
        .field("text", &Everything::text)
        .field("v2", &Everything::v2)
        .field("v3", &Everything::v3)
        .field("v4", &Everything::v4)
        .field("rotation", &Everything::rotation)
        .field("mode", &Everything::mode)
        .field("plain", &Everything::plain);
    REQUIRE(builder->status().has_value());
}
}

TEST_SUITE("reflect::TypeRegistry")
{
    TEST_CASE("finds a registered type by name and by C++ type")
    {
        TypeRegistry types;
        registerEverything(types);

        const TypeInfo* byName = types.find("Everything");
        REQUIRE(byName != nullptr);
        CHECK(byName->name == "Everything");
        CHECK(byName->category == TypeCategory::Component);
        CHECK(byName->fields.size() == 16);
        CHECK(types.find<Everything>() == byName);
        CHECK(types.find(std::type_index{ typeid(Everything) }) == byName);
    }

    TEST_CASE("does not find an unknown name or type")
    {
        TypeRegistry types;
        registerEverything(types);
        CHECK(types.find("Missing") == nullptr);
        CHECK(types.find<Unregistered>() == nullptr);
    }

    TEST_CASE("registers scripts next to components in registration order")
    {
        TypeRegistry types;
        registerEverything(types);
        auto script = types.script<Rotator>("Rotator");
        REQUIRE(script.has_value());
        script->field("speed", &Rotator::speed);

        REQUIRE(types.types().size() == 2);
        CHECK(types.types()[0]->name == "Everything");
        CHECK(types.types()[1]->name == "Rotator");
        CHECK(types.find("Rotator")->category == TypeCategory::Script);
    }

    TEST_CASE("rejects a duplicate type name")
    {
        TypeRegistry types;
        registerEverything(types);
        auto again = types.component<Rotator>("Everything");
        REQUIRE_FALSE(again.has_value());
        CHECK(again.error().code == ErrorCode::DuplicateName);
        CHECK(types.find<Rotator>() == nullptr);
        CHECK(types.types().size() == 1);
    }

    TEST_CASE("rejects a duplicate name across components and scripts")
    {
        TypeRegistry types;
        REQUIRE(types.component<Rotator>("Thing").has_value());
        auto again = types.script<Base>("Thing");
        REQUIRE_FALSE(again.has_value());
        CHECK(again.error().code == ErrorCode::DuplicateName);
    }

    TEST_CASE("rejects registering a C++ type twice")
    {
        TypeRegistry types;
        REQUIRE(types.component<Rotator>("Rotator").has_value());
        auto again = types.component<Rotator>("Spinner");
        REQUIRE_FALSE(again.has_value());
        CHECK(again.error().code == ErrorCode::DuplicateType);
        CHECK(types.find("Spinner") == nullptr);
    }

    TEST_CASE("rejects an empty type name")
    {
        TypeRegistry types;
        auto result = types.component<Rotator>("");
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::InvalidName);
    }

    TEST_CASE("rejects a duplicate field name and keeps the first")
    {
        TypeRegistry types;
        auto builder = types.component<Derived>("Derived");
        REQUIRE(builder.has_value());
        builder->field("value", &Derived::baseValue).field("value", &Derived::own);

        REQUIRE_FALSE(builder->status().has_value());
        CHECK(builder->status().error().code == ErrorCode::DuplicateName);
        REQUIRE(types.find("Derived")->fields.size() == 1);
        CHECK(types.find("Derived")->fields[0].kind == FieldKind::Int);
    }

    TEST_CASE("registers a field declared in a base class")
    {
        TypeRegistry types;
        auto builder = types.component<Derived>("Derived");
        REQUIRE(builder.has_value());
        builder->field("baseValue", &Base::baseValue).field("own", &Derived::own);
        CHECK(builder->status().has_value());

        Derived object;
        const TypeInfo* info = types.find<Derived>();
        REQUIRE(info->set(&object, "baseValue", std::int64_t{ 9 }).has_value());
        CHECK(object.baseValue == 9);
    }

    TEST_CASE("creates an instance of a registered type by name")
    {
        TypeRegistry types;
        registerEverything(types);
        const TypeInfo* info = types.find<Everything>();
        REQUIRE(info);
        auto created = info->create();
        REQUIRE(created);
        CHECK(info->set(created->get(), "integer", Value{ std::int64_t{ 5 } }));
        CHECK(static_cast<Everything*>(created->get())->integer == 5);
        CHECK(static_cast<Everything*>(info->create()->get()) != static_cast<Everything*>(created->get()));
    }

    TEST_CASE("cannot create a type that is not default constructible")
    {
        struct NoDefault
        {
            explicit NoDefault(int) {}
        };
        TypeRegistry types;
        REQUIRE(types.component<NoDefault>("NoDefault"));
        const auto created = types.find<NoDefault>()->create();
        REQUIRE_FALSE(created);
        CHECK(created.error().code == ErrorCode::NotConstructible);
    }

    TEST_CASE("converts an instance to a registered base, wherever the base sits")
    {
        struct First
        {
            virtual ~First() = default;
            int a{ 1 };
        };
        struct Second
        {
            virtual ~Second() = default;
            int b{ 2 };
        };
        struct Both : First, Second
        {
        };
        TypeRegistry types;
        {
            auto builder = types.component<Both>("Both");
            REQUIRE(builder);
            builder->base<First>().base<Second>();
        }
        const TypeInfo* info = types.find<Both>();
        auto both = std::make_shared<Both>();
        void* object = both.get();

        CHECK(info->as<First>(object) == static_cast<First*>(both.get()));
        CHECK(info->as<Second>(object) == static_cast<Second*>(both.get()));
        CHECK(static_cast<void*>(info->as<Second>(object)) != object); // Not the first base
        CHECK(info->as<Second>(object)->b == 2);
    }

    TEST_CASE("does not convert to a base that was not registered")
    {
        TypeRegistry types;
        REQUIRE(types.component<Derived>("Derived"));
        Derived value;
        CHECK(types.find<Derived>()->as<Base>(&value) == nullptr);
    }

    TEST_CASE("describes an error")
    {
        TypeRegistry types;
        REQUIRE(types.component<Rotator>("Rotator").has_value());
        auto again = types.component<Base>("Rotator");
        REQUIRE_FALSE(again.has_value());
        CHECK(describe(again.error()).starts_with("DuplicateName: "));
    }
}

TEST_SUITE("reflect::TypeInfo")
{
    TEST_CASE("gets every field type by name")
    {
        TypeRegistry types;
        registerEverything(types);
        Everything object;
        object.flag = true;
        object.small = -5;
        object.integer = -70000;
        object.big = -5'000'000'000;
        object.unsignedSmall = 65535;
        object.unsignedInt = 4'000'000'000u;
        object.unsignedBig = 18'000'000'000'000'000'000ull;
        object.real = 1.5f;
        object.precise = 0.1;
        object.text = "hello world";
        object.v2 = { 1.0f, 2.0f };
        object.v3 = { 1.0f, 2.0f, 3.0f };
        object.v4 = { 1.0f, 2.0f, 3.0f, 4.0f };
        object.rotation = glm::quat{ 0.5f, 0.5f, 0.5f, 0.5f };
        object.mode = Mode::On;
        object.plain = High;

        const TypeInfo* info = types.find<Everything>();
        auto get = [&](std::string_view field) { return info->get(&object, field); };

        CHECK(std::get<bool>(*get("flag")) == true);
        CHECK(std::get<std::int64_t>(*get("small")) == -5);
        CHECK(std::get<std::int64_t>(*get("integer")) == -70000);
        CHECK(std::get<std::int64_t>(*get("big")) == -5'000'000'000);
        CHECK(std::get<std::uint64_t>(*get("unsignedSmall")) == 65535);
        CHECK(std::get<std::uint64_t>(*get("unsignedInt")) == 4'000'000'000u);
        CHECK(std::get<std::uint64_t>(*get("unsignedBig")) == 18'000'000'000'000'000'000ull);
        CHECK(std::get<float>(*get("real")) == 1.5f);
        CHECK(std::get<double>(*get("precise")) == 0.1);
        CHECK(std::get<std::string>(*get("text")) == "hello world");
        CHECK(std::get<glm::vec2>(*get("v2")).y == 2.0f);
        CHECK(std::get<glm::vec3>(*get("v3")).z == 3.0f);
        CHECK(std::get<glm::vec4>(*get("v4")).w == 4.0f);
        CHECK(std::get<glm::quat>(*get("rotation")).w == 0.5f);
        CHECK(std::get<std::int64_t>(*get("mode")) == 3);
        CHECK(std::get<std::int64_t>(*get("plain")) == 7);
    }

    TEST_CASE("sets every field type by name")
    {
        TypeRegistry types;
        registerEverything(types);
        Everything object;
        const TypeInfo* info = types.find<Everything>();
        auto set = [&](std::string_view field, Value value) { CHECK(info->set(&object, field, value).has_value()); };

        set("flag", true);
        set("small", std::int64_t{ -5 });
        set("integer", std::int64_t{ -70000 });
        set("big", std::int64_t{ -5'000'000'000 });
        set("unsignedSmall", std::uint64_t{ 65535 });
        set("unsignedInt", std::uint64_t{ 4'000'000'000u });
        set("unsignedBig", std::uint64_t{ 18'000'000'000'000'000'000ull });
        set("real", 1.5f);
        set("precise", 0.1);
        set("text", std::string{ "hello world" });
        set("v2", glm::vec2{ 1.0f, 2.0f });
        set("v3", glm::vec3{ 1.0f, 2.0f, 3.0f });
        set("v4", glm::vec4{ 1.0f, 2.0f, 3.0f, 4.0f });
        set("rotation", glm::quat{ 0.5f, 0.5f, 0.5f, 0.5f });
        set("mode", std::int64_t{ 3 });
        set("plain", std::int64_t{ -1 });

        CHECK(object.flag == true);
        CHECK(object.small == -5);
        CHECK(object.integer == -70000);
        CHECK(object.big == -5'000'000'000);
        CHECK(object.unsignedSmall == 65535);
        CHECK(object.unsignedInt == 4'000'000'000u);
        CHECK(object.unsignedBig == 18'000'000'000'000'000'000ull);
        CHECK(object.real == 1.5f);
        CHECK(object.precise == 0.1);
        CHECK(object.text == "hello world");
        CHECK(object.v2.y == 2.0f);
        CHECK(object.v3.z == 3.0f);
        CHECK(object.v4.w == 4.0f);
        CHECK(object.rotation.w == 0.5f);
        CHECK(object.mode == Mode::On);
        CHECK(object.plain == Low);
    }

    TEST_CASE("accepts either integer alternative for an integer field")
    {
        TypeRegistry types;
        registerEverything(types);
        Everything object;
        const TypeInfo* info = types.find<Everything>();
        CHECK(info->set(&object, "integer", std::uint64_t{ 12 }).has_value());
        CHECK(object.integer == 12);
        CHECK(info->set(&object, "unsignedInt", std::int64_t{ 13 }).has_value());
        CHECK(object.unsignedInt == 13);
    }

    TEST_CASE("rejects a value of the wrong kind and leaves the field unchanged")
    {
        TypeRegistry types;
        registerEverything(types);
        Everything object;
        object.real = 2.0f;
        const TypeInfo* info = types.find<Everything>();

        auto result = info->set(&object, "real", std::string{ "nope" });
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::TypeMismatch);
        CHECK(object.real == 2.0f);
        CHECK(info->set(&object, "flag", std::int64_t{ 1 }).error().code == ErrorCode::TypeMismatch);
        CHECK(info->set(&object, "v3", glm::vec2{ 1.0f }).error().code == ErrorCode::TypeMismatch);
    }

    TEST_CASE("rejects an integer that does not fit the field")
    {
        TypeRegistry types;
        registerEverything(types);
        Everything object;
        const TypeInfo* info = types.find<Everything>();
        CHECK(info->set(&object, "small", std::int64_t{ 200 }).error().code == ErrorCode::OutOfRange);
        CHECK(info->set(&object, "unsignedSmall", std::int64_t{ -1 }).error().code == ErrorCode::OutOfRange);
        CHECK(info->set(&object, "integer", std::uint64_t{ 1ull << 40 }).error().code == ErrorCode::OutOfRange);
        CHECK(object.small == 0);
    }

    TEST_CASE("rejects a value that is not an enumerator")
    {
        TypeRegistry types;
        registerEverything(types);
        Everything object;
        const TypeInfo* info = types.find<Everything>();
        CHECK(info->set(&object, "mode", std::int64_t{ 1 }).error().code == ErrorCode::OutOfRange);
        CHECK(object.mode == Mode::Off);
    }

    TEST_CASE("reports an unknown field")
    {
        TypeRegistry types;
        registerEverything(types);
        Everything object;
        const TypeInfo* info = types.find<Everything>();
        CHECK(info->findField("missing") == nullptr);
        CHECK(info->get(&object, "missing").error().code == ErrorCode::UnknownField);
        CHECK(info->set(&object, "missing", true).error().code == ErrorCode::UnknownField);
        CHECK(info->getText(&object, "missing").error().code == ErrorCode::UnknownField);
        CHECK(info->setText(&object, "missing", "1").error().code == ErrorCode::UnknownField);
    }

    TEST_CASE("describes enum fields with their enumerators")
    {
        TypeRegistry types;
        registerEverything(types);
        const FieldInfo* mode = types.find<Everything>()->findField("mode");
        REQUIRE(mode != nullptr);
        CHECK(mode->kind == FieldKind::Enum);
        REQUIRE(mode->enumEntries.size() == 2);
        CHECK(mode->enumEntries[1].name == "On");
        CHECK(mode->enumEntries[1].value == 3);
    }
}

TEST_SUITE("reflect::text")
{
    // Sets `field` from `text`, reads it back as text, and expects the same text.
    TEST_CASE("round-trips every field type through text")
    {
        struct Case
        {
            std::string_view field;
            std::string_view text;
        };
        constexpr auto cases = std::to_array<Case>({
            { "flag", "true" },
            { "small", "-5" },
            { "integer", "-70000" },
            { "big", "-5000000000" },
            { "unsignedSmall", "65535" },
            { "unsignedInt", "4000000000" },
            { "unsignedBig", "18000000000000000000" },
            { "real", "1.5" },
            { "real", "0.1" },
            { "precise", "0.1" },
            { "precise", "-1e+300" },
            { "text", "hello world" },
            { "text", "" },
            { "v2", "1 2.5" },
            { "v3", "1 2 3" },
            { "v4", "1 2 3 4" },
            { "rotation", "0.1 0.2 0.3 0.4" },
            { "mode", "On" },
            { "plain", "Low" },
        });

        TypeRegistry types;
        registerEverything(types);
        const TypeInfo* info = types.find<Everything>();

        for (const auto& c : cases)
        {
            CAPTURE(c.field);
            CAPTURE(c.text);
            Everything object;
            REQUIRE(info->setText(&object, c.field, c.text).has_value());
            auto text = info->getText(&object, c.field);
            REQUIRE(text.has_value());
            CHECK(*text == c.text);
        }
    }

    TEST_CASE("reads a quaternion as x y z w")
    {
        TypeRegistry types;
        registerEverything(types);
        Everything object;
        REQUIRE(types.find<Everything>()->setText(&object, "rotation", "1 2 3 4").has_value());
        CHECK(object.rotation.x == 1.0f);
        CHECK(object.rotation.y == 2.0f);
        CHECK(object.rotation.z == 3.0f);
        CHECK(object.rotation.w == 4.0f);
    }

    TEST_CASE("writes the text of a set value")
    {
        TypeRegistry types;
        registerEverything(types);
        Everything object;
        object.mode = Mode::On;
        object.v3 = { 1.0f, 2.5f, -3.0f };
        object.flag = true;
        const TypeInfo* info = types.find<Everything>();
        CHECK(*info->getText(&object, "mode") == "On");
        CHECK(*info->getText(&object, "v3") == "1 2.5 -3");
        CHECK(*info->getText(&object, "flag") == "true");
    }

    TEST_CASE("rejects text that does not parse")
    {
        TypeRegistry types;
        registerEverything(types);
        Everything object;
        const TypeInfo* info = types.find<Everything>();
        CHECK(info->setText(&object, "flag", "yes").error().code == ErrorCode::ParseError);
        CHECK(info->setText(&object, "integer", "12x").error().code == ErrorCode::ParseError);
        CHECK(info->setText(&object, "integer", "").error().code == ErrorCode::ParseError);
        CHECK(info->setText(&object, "real", "abc").error().code == ErrorCode::ParseError);
        CHECK(info->setText(&object, "v3", "1 2").error().code == ErrorCode::ParseError);
        CHECK(info->setText(&object, "v3", "1 2 3 4").error().code == ErrorCode::ParseError);
        CHECK(info->setText(&object, "mode", "Sideways").error().code == ErrorCode::ParseError);
    }

    TEST_CASE("rejects text that does not fit the field")
    {
        TypeRegistry types;
        registerEverything(types);
        Everything object;
        const TypeInfo* info = types.find<Everything>();
        CHECK(info->setText(&object, "small", "200").error().code == ErrorCode::OutOfRange);
        CHECK(info->setText(&object, "unsignedSmall", "-1").error().code == ErrorCode::ParseError);
        CHECK(info->setText(&object, "big", "99999999999999999999").error().code == ErrorCode::OutOfRange);
    }
}

namespace
{
// Stands in for assets::AssetRef<T>: reflect only needs the ID and a way to make one from it.
struct FakeRef
{
    std::uint64_t id{ 0 };
};

struct Holder
{
    FakeRef mesh;
};
}

template<>
struct aegis::reflect::AssetFieldTraits<FakeRef>
{
    static auto id(const FakeRef& ref) -> std::uint64_t { return ref.id; }
    static auto make(std::uint64_t id) -> FakeRef { return FakeRef{ id }; }
};

TEST_SUITE("reflect::AssetRef")
{
    TEST_CASE("an asset reference field is written as its ID")
    {
        TypeRegistry types;
        auto builder = types.component<Holder>("Holder");
        REQUIRE(builder.has_value());
        builder->field("mesh", &Holder::mesh);
        REQUIRE(builder->status().has_value());

        const TypeInfo* info = types.find<Holder>();
        const FieldInfo* mesh = info->findField("mesh");
        REQUIRE(mesh != nullptr);
        CHECK(mesh->kind == FieldKind::AssetRef);

        Holder holder{ FakeRef{ 0xa3f09c2e4b1d7788ULL } };
        CHECK(info->getText(&holder, "mesh").value() == "a3f09c2e4b1d7788");

        Holder other;
        REQUIRE(info->setText(&other, "mesh", "00000000000000ab").has_value());
        CHECK(other.mesh.id == 0xab);
    }

    TEST_CASE("an empty reference is empty text and parses back")
    {
        TypeRegistry types;
        auto builder = types.component<Holder>("Holder");
        builder->field("mesh", &Holder::mesh);
        const TypeInfo* info = types.find<Holder>();

        Holder holder;
        CHECK(info->getText(&holder, "mesh").value().empty());
        holder.mesh.id = 5;
        REQUIRE(info->setText(&holder, "mesh", "").has_value());
        CHECK(holder.mesh.id == 0);
    }

    TEST_CASE("rejects asset IDs of the wrong width or case")
    {
        TypeRegistry types;
        auto builder = types.component<Holder>("Holder");
        builder->field("mesh", &Holder::mesh);
        const TypeInfo* info = types.find<Holder>();

        Holder holder;
        CHECK_FALSE(info->setText(&holder, "mesh", "abc").has_value());
        CHECK_FALSE(info->setText(&holder, "mesh", "A3F09C2E4B1D7788").has_value());
        CHECK_FALSE(info->setText(&holder, "mesh", "a3f09c2e4b1d778g").has_value());
    }

    TEST_CASE("an integer value is not an asset reference")
    {
        TypeRegistry types;
        auto builder = types.component<Holder>("Holder");
        builder->field("mesh", &Holder::mesh);
        const TypeInfo* info = types.find<Holder>();

        Holder holder;
        CHECK_FALSE(info->set(&holder, "mesh", std::int64_t{ 5 }).has_value());
        CHECK(info->set(&holder, "mesh", std::uint64_t{ 5 }).has_value());
    }
}
