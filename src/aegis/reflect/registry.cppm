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
// every call returns the builder, so fields chain. An error (a duplicate name) does not stop the
// chain: the first one is kept and reported by status(), and the offending call changes nothing.
template<class T>
class TypeBuilder
{
public:
    // Registers `member` (a data member of T or of a base of T) under `name`, with the editor metadata
    // `meta`. The member's type must have a FieldConversion; an unsupported type is a compile error here.
    // Metadata that does not fit the field's kind fails with InvalidMeta, and the field is not added.
    template<class Owner, class Member>
        requires std::derived_from<T, Owner>
    auto field(std::string_view name, Member Owner::* member, FieldMeta meta = {}) -> TypeBuilder&
    {
        using Conversion = FieldConversion<Member>;

        if (m_info->findField(name))
            return fail(ErrorCode::DuplicateName, std::format("type '{}' already has a field '{}'", m_info->name, name));
        if (auto problem = metaProblem(Conversion::kind, meta); !problem.empty())
            return fail(ErrorCode::InvalidMeta, std::format("field '{}' of type '{}': {}", name, m_info->name, problem));

        FieldInfo info{ .name = std::string{ name }, .kind = Conversion::kind, .meta = std::move(meta) };
        if constexpr (requires { Conversion::enumEntries(); })
            info.enumEntries = Conversion::enumEntries();
        if constexpr (requires { Conversion::assetType(); })
            info.assetType = std::string{ Conversion::assetType() };
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

    // The name an editor shows for the type (TypeInfo::displayName), when it differs from the registered one.
    auto displayName(std::string_view displayName) -> TypeBuilder&
    {
        m_info->meta.displayName = displayName;
        return *this;
    }

    // The group the editor lists the type under ("Lighting").
    auto category(std::string_view category) -> TypeBuilder&
    {
        m_info->meta.category = category;
        return *this;
    }

    // Marks the type as one every entity has and none can remove (TypeMeta::required).
    auto required() -> TypeBuilder&
    {
        m_info->meta.required = true;
        return *this;
    }

    // Records B as a base of T, so TypeInfo::as<B> can convert an instance's address to a B*.
    template<class B>
    auto base() -> TypeBuilder&
    {
        static_assert(std::derived_from<T, B>, "B must be a base class of T");
        m_info->upcasts[std::type_index{ typeid(B) }] = [](void* object) -> void* {
            return static_cast<B*>(static_cast<T*>(object));
        };
        return *this;
    }

    // Sets the version of the type's saved form (1 by default). Raise it when the saved form changes, and
    // add the migration from the previous version. Call it before adding migrations.
    auto version(std::uint32_t version) -> TypeBuilder&
    {
        if (version == 0)
            return fail(ErrorCode::InvalidVersion, std::format("type '{}' needs a version of at least 1", m_info->name));
        if (!m_info->migrations.empty() && m_info->migrations.rbegin()->first >= version)
            return fail(ErrorCode::InvalidVersion, std::format("type '{}' has a migration past version {}", m_info->name, version));
        m_info->version = version;
        return *this;
    }

    // Adds the migration from version `from` to `from + 1`; `from` must be below the type's version.
    auto migration(std::uint32_t from, Migration migrate) -> TypeBuilder&
    {
        if (from == 0 || from >= m_info->version)
            return fail(ErrorCode::InvalidVersion, std::format("type '{}' is at version {}: no migration from version {}", m_info->name, m_info->version, from));
        if (m_info->migrations.contains(from))
            return fail(ErrorCode::InvalidVersion, std::format("type '{}' already has a migration from version {}", m_info->name, from));
        m_info->migrations.emplace(from, std::move(migrate));
        return *this;
    }

    // Records an earlier name of the type, so TypeRegistry::find still finds it under that name.
    auto alias(std::string_view oldName) -> TypeBuilder&;

    // Records an earlier name of the field `name`, so files that use it still load.
    auto fieldAlias(std::string_view oldName, std::string_view name) -> TypeBuilder&
    {
        if (!m_info->findField(name))
            return fail(ErrorCode::UnknownField, std::format("type '{}' has no field '{}' to alias", m_info->name, name));
        if (m_info->findField(oldName))
            return fail(ErrorCode::DuplicateName, std::format("type '{}' already has a field '{}'", m_info->name, oldName));
        m_info->fieldAliases.emplace(std::string{ oldName }, std::string{ name });
        return *this;
    }

    // Attaches `data` to the type, found again with TypeInfo::attachment<X>(). A later attach replaces it.
    template<class X>
    auto attach(X data) -> TypeBuilder&
    {
        m_info->attachments[std::type_index{ typeid(X) }] = std::make_shared<const X>(std::move(data));
        return *this;
    }

    // Ok while every call so far succeeded.
    [[nodiscard]] auto status() const -> const std::expected<void, Error>& { return m_status; }

    [[nodiscard]] auto info() const -> const TypeInfo& { return *m_info; }

private:
    friend class TypeRegistry;
    TypeBuilder(TypeRegistry* registry, TypeInfo* info) :
        m_registry(registry),
        m_info(info)
    {
    }

    // Why `meta` does not fit a field of `kind`, or empty if it does.
    [[nodiscard]] static auto metaProblem(FieldKind kind, const FieldMeta& meta) -> std::string_view
    {
        const bool scalar = kind == FieldKind::Int || kind == FieldKind::UInt || kind == FieldKind::Float || kind == FieldKind::Double;
        const bool vector = kind == FieldKind::Vec2 || kind == FieldKind::Vec3 || kind == FieldKind::Vec4;
        const bool limited = meta.min || meta.max || meta.step;
        if (limited && !scalar && !vector)
            return "only numbers and vectors take a min, max or step";
        if (meta.min && meta.max && *meta.min > *meta.max)
            return "min is above max";
        if (meta.step && !(*meta.step > 0.0))
            return "step must be positive";

        switch (meta.widget)
        {
        case FieldWidget::Default:
            return {};
        case FieldWidget::Color:
            return kind == FieldKind::Vec3 || kind == FieldKind::Vec4 ? std::string_view{} : "a Color widget needs a Vec3 or Vec4 field";
        case FieldWidget::Angle:
            return kind == FieldKind::Float || kind == FieldKind::Double ? std::string_view{} : "an Angle widget needs a Float or Double field";
        case FieldWidget::Slider:
            if (!scalar)
                return "a Slider widget needs a number field";
            return meta.min && meta.max ? std::string_view{} : "a Slider widget needs a min and a max";
        }
        return {};
    }

    auto fail(ErrorCode code, std::string message) -> TypeBuilder&
    {
        if (m_status)
            m_status = makeError(code, std::move(message));
        return *this;
    }

    TypeRegistry* m_registry;
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
    friend class TypeBuilder;

    // Makes `name` find `info` too. Fails like a registration under that name.
    auto addAlias(std::string_view name, TypeInfo& info) -> std::expected<void, Error>
    {
        if (name.empty())
            return makeError(ErrorCode::InvalidName, "a type alias needs a name");
        if (find(name))
            return makeError(ErrorCode::DuplicateName, std::format("a type named '{}' is already registered", name));
        info.aliases.emplace_back(name);
        m_byName.emplace(std::string{ name }, &info);
        return {};
    }

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
        if constexpr (std::default_initializable<T>)
            info.construct = [] { return std::shared_ptr<void>{ std::make_shared<T>() }; };
        m_byName.emplace(info.name, &info);
        m_byType.emplace(info.type, &info);
        m_ordered.push_back(&info);
        return TypeBuilder<T>{ this, &info };
    }

    std::vector<std::unique_ptr<TypeInfo>> m_storage;
    std::vector<const TypeInfo*> m_ordered;
    std::map<std::string, const TypeInfo*, std::less<>> m_byName;
    std::unordered_map<std::type_index, const TypeInfo*> m_byType;
};

template<class T>
auto TypeBuilder<T>::alias(std::string_view oldName) -> TypeBuilder&
{
    if (auto added = m_registry->addAlias(oldName, *m_info); !added && m_status)
        m_status = std::unexpected{ added.error() };
    return *this;
}
}
