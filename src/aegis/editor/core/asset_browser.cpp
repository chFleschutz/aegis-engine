module aegis.editor.core;
import std;
import aegis.core;
import aegis.math;
import aegis.serialize;

namespace aegis::editor
{
namespace
{
auto fileName(std::string_view path) -> std::string_view
{
    const auto slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

auto parentPath(std::string_view path) -> std::string_view
{
    const auto slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string_view{} : path.substr(0, slash);
}
}

auto AssetFilter::matches(const AssetItem& item) const -> bool
{
    if (!type.empty() && item.type != type)
        return false;
    return core::containsIgnoringCase(fileName(item.path), search) || core::containsIgnoringCase(item.key, search);
}

auto AssetTree::build(std::vector<AssetItem> items, std::vector<AssetRoot> roots) -> AssetTree
{
    AssetTree tree;
    tree.m_items = std::move(items);
    tree.m_roots = std::move(roots);

    std::map<std::pair<std::size_t, std::string>, std::size_t> folders;
    std::map<std::pair<std::size_t, std::string>, std::size_t> files;
    for (std::size_t root = 0; root < tree.m_roots.size(); ++root)
    {
        tree.m_folders.push_back(AssetFolder{ .root = root, .name = tree.m_roots[root].name });
        folders.emplace(std::pair{ root, std::string{} }, root);
    }

    // The folder at 'path', created with its ancestors
    const auto folderAt = [&](this const auto& self, std::size_t root, std::string_view path) -> std::size_t {
        if (const auto found = folders.find(std::pair{ root, std::string{ path } }); found != folders.end())
            return found->second;
        const auto parent = self(root, parentPath(path));
        const auto index = tree.m_folders.size();
        tree.m_folders.push_back(AssetFolder{ .root = root, .path = std::string{ path }, .name = std::string{ fileName(path) }, .parent = parent });
        tree.m_folders[parent].folders.push_back(index);
        folders.emplace(std::pair{ root, std::string{ path } }, index);
        return index;
    };

    for (std::size_t i = 0; i < tree.m_items.size(); ++i)
    {
        const auto& item = tree.m_items[i];
        if (item.root >= tree.m_roots.size())
            continue;
        auto [found, added] = files.try_emplace(std::pair{ item.root, item.path }, tree.m_files.size());
        if (added)
        {
            const auto folder = folderAt(item.root, parentPath(item.path));
            tree.m_folders[folder].files.push_back(found->second);
            tree.m_files.push_back(AssetFile{ .root = item.root, .path = item.path, .name = std::string{ fileName(item.path) } });
        }
        auto& file = tree.m_files[found->second];
        if (item.key.empty())
            file.main = i;
        else
            file.parts.push_back(i);
    }

    for (auto& file : tree.m_files)
        std::ranges::sort(file.parts, {}, [&tree](std::size_t i) -> const std::string& { return tree.m_items[i].key; });
    for (auto& folder : tree.m_folders)
    {
        std::ranges::sort(folder.folders, {}, [&tree](std::size_t i) -> const std::string& { return tree.m_folders[i].name; });
        std::ranges::sort(folder.files, {}, [&tree](std::size_t i) -> const std::string& { return tree.m_files[i].name; });
    }
    return tree;
}

auto AssetTree::findFolder(std::size_t root, std::string_view path) const -> std::optional<std::size_t>
{
    const auto found = std::ranges::find_if(m_folders, [&](const AssetFolder& folder) { return folder.root == root && folder.path == path; });
    return found != m_folders.end() ? std::optional{ static_cast<std::size_t>(found - m_folders.begin()) } : std::nullopt;
}

auto AssetTree::types() const -> std::vector<std::string>
{
    std::vector<std::string> types;
    for (const auto& item : m_items)
        types.push_back(item.type);
    std::ranges::sort(types);
    const auto [first, last] = std::ranges::unique(types);
    types.erase(first, last);
    std::erase(types, std::string{});
    return types;
}

auto AssetTree::rows(std::size_t folder, const AssetFilter& filter) const -> std::vector<AssetRow>
{
    std::vector<AssetRow> rows;
    if (folder >= m_folders.size())
        return rows;
    if (!filter.active())
    {
        for (const auto file : m_folders[folder].files)
            rows.push_back(AssetRow{ .file = file, .parts = m_files[file].parts, .mainMatches = m_files[file].main.has_value() });
        return rows;
    }
    collectRows(folder, filter, rows);
    return rows;
}

void AssetTree::collectRows(std::size_t folder, const AssetFilter& filter, std::vector<AssetRow>& rows) const
{
    for (const auto index : m_folders[folder].files)
    {
        const auto& file = m_files[index];
        AssetRow row{ .file = index, .mainMatches = file.main && filter.matches(m_items[*file.main]) };
        for (const auto part : file.parts)
        {
            if (filter.matches(m_items[part]))
                row.parts.push_back(part);
        }
        if (row.mainMatches || !row.parts.empty())
            rows.push_back(std::move(row));
    }
    for (const auto child : m_folders[folder].folders)
        collectRows(child, filter, rows);
}

auto AssetPayload::assetType() const -> std::string_view
{
    return { type.data(), static_cast<std::size_t>(std::ranges::find(type, '\0') - type.begin()) };
}

auto makeAssetPayload(std::uint64_t id, std::string_view type) -> AssetPayload
{
    AssetPayload payload{ .id = id };
    const auto size = std::min(type.size(), payload.type.size() - 1);
    std::ranges::copy(type.substr(0, size), payload.type.begin());
    return payload;
}

auto acceptsAsset(std::string_view slotType, const AssetPayload& payload) -> bool
{
    return payload.id != 0 && !slotType.empty() && payload.assetType() == slotType;
}

auto canInstantiate(const AssetPayload& payload) -> bool
{
    return payload.id != 0 && payload.assetType() == serialize::Scene::assetType;
}

auto dropPoint(const Ray& ray, float maxDistance, float fallbackDistance) -> glm::vec3
{
    // Z-up: the ground is z = 0; a ray parallel to it, or away from it, never meets it
    if (ray.direction.z != 0.0f)
    {
        const float distance = -ray.origin.z / ray.direction.z;
        if (distance > 0.0f && distance <= maxDistance)
            return ray.origin + ray.direction * distance;
    }
    return ray.origin + ray.direction * fallbackDistance;
}
}
