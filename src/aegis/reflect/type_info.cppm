export module aegis.reflect:type_info;
import std;
import :error;
import :node;
import :value;

export namespace aegis::reflect
{
// One registered member: its name, kind, and type-erased accessors. The accessors take the address of
// an instance of the owning type; passing an instance of another type is the caller's bug (look the
// TypeInfo up with the same type or name you are holding the object for).
struct FieldInfo
{
    std::string name;
    FieldKind kind;
    std::vector<EnumEntry> enumEntries; // Only for FieldKind::Enum

    std::function<Value(const void* object)> getter;
    std::function<std::expected<void, Error>(void* object, const Value& value)> setter;
};

enum class TypeCategory
{
    Component, // Data attached to an entity
    Script,    // Behavior attached to an entity, with its persistent state in fields
};

// Upgrades a type's saved fields (an object node, as read from a file) by one version, from n to n + 1.
using Migration = std::function<std::expected<void, Error>(Node& fields)>;

// The description of one registered type. Immutable once registered, and its address is stable for
// the registry's lifetime.
struct TypeInfo
{
    std::string name;
    TypeCategory category;
    std::type_index type;
    std::vector<FieldInfo> fields; // In registration order

    // The version of the type's saved form, starting at 1. Files record it per type; loading an older one
    // runs the migrations from it up to this one (see upgrade).
    std::uint32_t version{ 1 };
    // migrations[n] upgrades version n to n + 1.
    std::map<std::uint32_t, Migration> migrations;

    // Earlier names of the type (found by TypeRegistry::find) and of its fields (`old name -> name`), so
    // files written before a rename still load. Files are written with the current names.
    std::vector<std::string> aliases;
    std::map<std::string, std::string, std::less<>> fieldAliases;

    // Data a layer above reflect keeps per type, by its C++ type (the world's access to a component's
    // storage, say). See TypeBuilder::attach.
    std::map<std::type_index, std::shared_ptr<const void>> attachments;

    // Creates a default-constructed instance, owned by the returned pointer (which destroys it as the
    // registered type). Empty when the type is not default constructible. A caller that knows the
    // registered type's base class converts it to that base: for a script, whose first base is the
    // script base class, the address is the same.
    std::function<std::shared_ptr<void>()> construct;

    // Converts a pointer to an instance of this type to a pointer to its base B, for the bases recorded
    // with TypeBuilder::base. The conversion is a real upcast, so it is right for any inheritance layout.
    std::map<std::type_index, std::function<void*(void*)>> upcasts;

    // `object` points to an instance of this type. Null if B was not registered as a base.
    template<class B>
    [[nodiscard]] auto as(void* object) const -> B*
    {
        auto it = upcasts.find(std::type_index{ typeid(B) });
        return it == upcasts.end() ? nullptr : static_cast<B*>(it->second(object));
    }

    // Creates an instance of the type, or fails with ErrorCode::NotConstructible.
    [[nodiscard]] auto create() const -> std::expected<std::shared_ptr<void>, Error>
    {
        if (!construct)
            return makeError(ErrorCode::NotConstructible, std::format("type '{}' is not default constructible", name));
        return construct();
    }

    // The data attached as X, or null.
    template<class X>
    [[nodiscard]] auto attachment() const -> const X*
    {
        auto it = attachments.find(std::type_index{ typeid(X) });
        return it == attachments.end() ? nullptr : static_cast<const X*>(it->second.get());
    }

    // The field named `fieldName` or, failing that, the field it is an alias of.
    [[nodiscard]] auto findField(std::string_view fieldName) const -> const FieldInfo*
    {
        for (const auto& field : fields)
        {
            if (field.name == fieldName)
                return &field;
        }
        if (auto alias = fieldAliases.find(fieldName); alias != fieldAliases.end())
            return findField(alias->second);
        return nullptr;
    }

    // Brings `fields` (an object node of this type, saved at version `from`) up to the current version, one
    // migration at a time. Fails with UnsupportedVersion for version 0, a version newer than this one or a
    // missing step; a failing migration's error is returned as it is.
    [[nodiscard]] auto upgrade(Node& fields, std::uint32_t from) const -> std::expected<void, Error>
    {
        if (from == 0 || from > version)
        {
            return makeError(ErrorCode::UnsupportedVersion,
                std::format("type '{}' has no version {} (the current one is {})", name, from, version));
        }
        for (std::uint32_t step = from; step < version; ++step)
        {
            auto migration = migrations.find(step);
            if (migration == migrations.end())
                return makeError(ErrorCode::UnsupportedVersion, std::format("type '{}' has no migration from version {}", name, step));
            if (auto migrated = migration->second(fields); !migrated)
                return migrated;
        }
        return {};
    }

    // By-name access to a field of `object`, an instance of this type.
    [[nodiscard]] auto get(const void* object, std::string_view fieldName) const -> std::expected<Value, Error>
    {
        const FieldInfo* field = findField(fieldName);
        if (!field)
            return unknownField(fieldName);
        return field->getter(object);
    }

    [[nodiscard]] auto set(void* object, std::string_view fieldName, const Value& value) const -> std::expected<void, Error>
    {
        const FieldInfo* field = findField(fieldName);
        if (!field)
            return unknownField(fieldName);
        return field->setter(object, value);
    }

    // The same through the text form (see toText / fromText): what a serializer reads and writes.
    [[nodiscard]] auto getText(const void* object, std::string_view fieldName) const -> std::expected<std::string, Error>
    {
        const FieldInfo* field = findField(fieldName);
        if (!field)
            return unknownField(fieldName);
        return toText(field->kind, field->getter(object), field->enumEntries);
    }

    [[nodiscard]] auto setText(void* object, std::string_view fieldName, std::string_view text) const -> std::expected<void, Error>
    {
        const FieldInfo* field = findField(fieldName);
        if (!field)
            return unknownField(fieldName);
        auto value = fromText(field->kind, text, field->enumEntries);
        if (!value)
            return std::unexpected{ value.error() };
        return field->setter(object, *value);
    }

private:
    [[nodiscard]] auto unknownField(std::string_view fieldName) const -> std::unexpected<Error>
    {
        return makeError(ErrorCode::UnknownField, std::format("type '{}' has no field '{}'", name, fieldName));
    }
};
}
