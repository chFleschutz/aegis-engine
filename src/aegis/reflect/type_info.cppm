export module aegis.reflect:type_info;
import std;
import :error;
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

// The description of one registered type. Immutable once registered, and its address is stable for
// the registry's lifetime.
struct TypeInfo
{
    std::string name;
    TypeCategory category;
    std::type_index type;
    std::vector<FieldInfo> fields; // In registration order

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

    [[nodiscard]] auto findField(std::string_view fieldName) const -> const FieldInfo*
    {
        for (const auto& field : fields)
        {
            if (field.name == fieldName)
                return &field;
        }
        return nullptr;
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
