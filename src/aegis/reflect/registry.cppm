export module aegis.reflect:registry;
import std;
import :conversion;
import :error;
import :type_info;
import :value;

export namespace aegis::reflect
{
class TypeRegistry;

// Adds fields to a type that was just registered. Returned by TypeRegistry::component / script;
// every call returns the builder, so fields chain. A field error (a duplicate name) does not stop the
// chain: the first one is kept and reported by status(), and the offending field is not added.
template<class T>
class TypeBuilder
{
public:
    // Registers `member` (a data member of T or of a base of T) under `name`. The member's type must
    // have a FieldConversion; an unsupported type is a compile error here.
    template<class Owner, class Member>
        requires std::derived_from<T, Owner>
    auto field(std::string_view name, Member Owner::* member) -> TypeBuilder&
    {
        using Conversion = FieldConversion<Member>;

        if (m_info->findField(name))
        {
            if (m_status)
                m_status = makeError(ErrorCode::DuplicateName, std::format("type '{}' already has a field '{}'", m_info->name, name));
            return *this;
        }

        FieldInfo info{ .name = std::string{ name }, .kind = Conversion::kind };
        if constexpr (requires { Conversion::enumEntries(); })
            info.enumEntries = Conversion::enumEntries();
        info.getter = [member](const void* object) -> Value { return Conversion::toValue(static_cast<const T*>(object)->*member); };
        info.setter = [member](void* object, const Value& value) -> std::expected<void, Error> {
            auto converted = Conversion::fromValue(value);
            if (!converted)
                return std::unexpected{ converted.error() };
            static_cast<T*>(object)->*member = std::move(*converted);
            return {};
        };
        m_info->fields.push_back(std::move(info));
        return *this;
    }

    // Ok while every field so far was added.
    [[nodiscard]] auto status() const -> const std::expected<void, Error>& { return m_status; }

    [[nodiscard]] auto info() const -> const TypeInfo& { return *m_info; }

private:
    friend class TypeRegistry;
    explicit TypeBuilder(TypeInfo* info) :
        m_info(info)
    {
    }

    TypeInfo* m_info;
    std::expected<void, Error> m_status;
};

// The set of registered components and scripts, found by name or by C++ type. Names are unique
// across both categories, and a C++ type registers once. Registration is expected at startup (the
// registry is not synchronized); after that it is read-only. TypeInfo pointers stay valid until the
// registry is destroyed.
class TypeRegistry
{
public:
    // Registers `T` as a component named `name`. Fails with DuplicateName / DuplicateType.
    template<class T>
    [[nodiscard]] auto component(std::string_view name) -> std::expected<TypeBuilder<T>, Error>
    {
        return add<T>(name, TypeCategory::Component);
    }

    // Registers `T` as a script named `name`; its persistent state goes in fields.
    template<class T>
    [[nodiscard]] auto script(std::string_view name) -> std::expected<TypeBuilder<T>, Error>
    {
        return add<T>(name, TypeCategory::Script);
    }

    [[nodiscard]] auto find(std::string_view name) const -> const TypeInfo*
    {
        auto it = m_byName.find(name);
        return it == m_byName.end() ? nullptr : it->second;
    }

    [[nodiscard]] auto find(std::type_index type) const -> const TypeInfo*
    {
        auto it = m_byType.find(type);
        return it == m_byType.end() ? nullptr : it->second;
    }

    template<class T>
    [[nodiscard]] auto find() const -> const TypeInfo*
    {
        return find(std::type_index{ typeid(T) });
    }

    // Every registered type, in registration order.
    [[nodiscard]] auto types() const -> std::span<const TypeInfo* const> { return m_ordered; }

private:
    template<class T>
    auto add(std::string_view name, TypeCategory category) -> std::expected<TypeBuilder<T>, Error>
    {
        if (name.empty())
            return makeError(ErrorCode::InvalidName, "a type needs a name");
        if (find(name))
            return makeError(ErrorCode::DuplicateName, std::format("a type named '{}' is already registered", name));
        if (const TypeInfo* existing = find<T>())
            return makeError(ErrorCode::DuplicateType, std::format("the type is already registered as '{}'", existing->name));

        auto& info = *m_storage.emplace_back(std::make_unique<TypeInfo>(TypeInfo{ .name = std::string{ name }, .category = category, .type = std::type_index{ typeid(T) } }));
        m_byName.emplace(info.name, &info);
        m_byType.emplace(info.type, &info);
        m_ordered.push_back(&info);
        return TypeBuilder<T>{ &info };
    }

    std::vector<std::unique_ptr<TypeInfo>> m_storage;
    std::vector<const TypeInfo*> m_ordered;
    std::map<std::string, const TypeInfo*, std::less<>> m_byName;
    std::unordered_map<std::type_index, const TypeInfo*> m_byType;
};
}
