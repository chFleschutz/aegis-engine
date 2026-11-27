module aegis.editor.core;
import std;
import aegis.reflect;
import aegis.serialize;

namespace aegis::editor
{
namespace
{
using reflect::Node;

// The drag step of a material's numbers: their usual range is 0 to 1.
constexpr double numberStep{ 0.01 };

// Whether two file values mean the same: numbers by value (a hand-written "0.50" is the editor's 0.5), an object's
// members whatever their order.
auto sameNode(const Node& a, const Node& b) -> bool
{
    if (a.kind != b.kind)
        return false;
    switch (a.kind)
    {
    case Node::Kind::Null: return true;
    case Node::Kind::Bool: return a.boolean == b.boolean;
    case Node::Kind::Number:
    {
        const auto left = a.number();
        const auto right = b.number();
        return left && right ? *left == *right : a.text == b.text;
    }
    case Node::Kind::String: return a.text == b.text;
    case Node::Kind::Array: return std::ranges::equal(a.items, b.items, sameNode);
    case Node::Kind::Object:
        return a.items.size() == b.items.size() && std::ranges::all_of(a.items, [&b](const Node& member) {
            const Node* other = b.find(member.key);
            return other && sameNode(member, *other);
        });
    }
    return false;
}
}

MaterialDocument::MaterialDocument(std::uint64_t asset, std::string path, serialize::MaterialFile file) :
    m_asset{ asset },
    m_path{ std::move(path) },
    m_file{ file },
    m_saved{ std::move(file) }
{
}

auto MaterialDocument::fileName() const -> std::string_view
{
    const auto slash = m_path.rfind('/');
    return slash == std::string::npos ? std::string_view{ m_path } : std::string_view{ m_path }.substr(slash + 1);
}

auto MaterialDocument::parameter(std::string_view name) const -> const Node*
{
    return m_file.parameters.find(name);
}

auto MaterialDocument::setParameter(std::string_view name, std::optional<Node> value) -> bool
{
    auto& items = m_file.parameters.items;
    const auto found = std::ranges::find(items, name, &Node::key);
    if (!value)
    {
        if (found == items.end())
            return false;
        items.erase(found);
        return true;
    }
    value->key = std::string{ name };
    if (found == items.end())
    {
        items.push_back(std::move(*value));
        return true;
    }
    if (sameParameter(*found, value))
        return false;
    *found = std::move(*value);
    return true;
}

auto MaterialDocument::dirty() const -> bool
{
    return m_file.id != m_saved.id || m_file.templateId != m_saved.templateId || !sameNode(m_file.parameters, m_saved.parameters);
}

void MaterialDocument::markSaved()
{
    m_saved = m_file;
}

auto MaterialDocument::revert() -> bool
{
    if (!dirty())
        return false;
    m_file = m_saved;
    return true;
}

auto MaterialDocuments::open(std::uint64_t asset, std::string path, serialize::MaterialFile file) -> MaterialDocument&
{
    if (auto* document = find(asset))
        return *document;
    return m_documents.try_emplace(asset, asset, std::move(path), std::move(file)).first->second;
}

auto MaterialDocuments::find(std::uint64_t asset) -> MaterialDocument*
{
    const auto found = m_documents.find(asset);
    return found == m_documents.end() ? nullptr : &found->second;
}

auto MaterialDocuments::find(std::uint64_t asset) const -> const MaterialDocument*
{
    const auto found = m_documents.find(asset);
    return found == m_documents.end() ? nullptr : &found->second;
}

auto MaterialDocuments::dirty() const -> bool
{
    return std::ranges::any_of(m_documents, [](const auto& entry) { return entry.second.dirty(); });
}

auto MaterialDocuments::unsaved() -> std::vector<MaterialDocument*>
{
    std::vector<MaterialDocument*> documents;
    for (auto& [asset, document] : m_documents)
    {
        if (document.dirty())
            documents.push_back(&document);
    }
    return documents;
}

auto MaterialDocuments::revert() -> std::vector<std::uint64_t>
{
    std::vector<std::uint64_t> reverted;
    for (auto& [asset, document] : m_documents)
    {
        if (document.revert())
            reverted.push_back(asset);
    }
    return reverted;
}

auto parameterValue(const MaterialDocument& document, const MaterialParameterInfo& parameter) -> reflect::Value
{
    const Node* node = document.parameter(parameter.name);
    if (!node)
        return parameter.defaultValue;
    auto value = reflect::fromNode(parameter.kind, *node);
    return value ? *value : parameter.defaultValue;
}

auto parameterNode(const MaterialParameterInfo& parameter, const reflect::Value& value) -> std::expected<std::optional<Node>, Error>
{
    if (parameter.kind == reflect::FieldKind::AssetRef)
    {
        const auto* id = std::get_if<std::uint64_t>(&value);
        if (id && *id == 0)
            return std::optional<Node>{};
    }
    auto node = reflect::toNode(parameter.kind, value);
    if (!node)
        return makeError(node.error(), std::format("cannot write the value of '{}'", parameter.name));
    return std::optional<Node>{ std::move(*node) };
}

auto sameParameter(const std::optional<Node>& a, const std::optional<Node>& b) -> bool
{
    if (!a || !b)
        return !a && !b;
    return sameNode(*a, *b);
}

auto materialType(std::span<const MaterialParameterInfo> parameters) -> reflect::TypeInfo
{
    reflect::TypeInfo type{ .name = "Material", .category = reflect::TypeCategory::Settings, .type = typeid(MaterialDocument) };
    for (const auto& parameter : parameters)
    {
        reflect::FieldInfo field{ .name = parameter.name, .kind = parameter.kind };
        if (parameter.kind == reflect::FieldKind::AssetRef)
            field.assetType = "Texture";
        else if (parameter.kind == reflect::FieldKind::Vec3)
            field.meta.widget = reflect::FieldWidget::Color;
        else
            field.meta.step = numberStep;
        field.getter = [parameter](const void* object) { return parameterValue(*static_cast<const MaterialDocument*>(object), parameter); };
        type.fields.push_back(std::move(field));
    }
    return type;
}

auto newMaterialPath(std::string_view folder, const std::function<bool(std::string_view path)>& taken) -> std::string
{
    const std::string prefix = folder.empty() ? std::string{} : std::format("{}/", folder);
    std::string path = std::format("{}New Material.material", prefix);
    for (int number = 2; taken(path); ++number)
        path = std::format("{}New Material {}.material", prefix, number);
    return path;
}
}
