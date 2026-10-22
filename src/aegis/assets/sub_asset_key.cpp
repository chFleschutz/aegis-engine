module aegis.assets;
import std;
import aegis.base;

namespace aegis::assets
{
auto escapeKeyName(std::string_view name) -> std::string
{
    std::string escaped;
    escaped.reserve(name.size());
    for (const char c : name)
    {
        if (c == '\\' || c == '/' || c == '#')
            escaped.push_back('\\');
        escaped.push_back(c);
    }
    return escaped;
}

auto SubAssetKeys::name(std::string_view type, std::string_view name, std::size_t position) -> std::expected<std::string, Error>
{
    std::string problem;
    std::string key;
    if (name.empty())
    {
        key = std::format("{}/#{}", type, position);
        problem = std::format("{} {} has no name; its key is '{}'", type, position, key);
    }
    else
    {
        key = std::format("{}/{}", type, escapeKeyName(name));
        const auto count = m_seen[key]++;
        if (count > 0)
        {
            problem = std::format("{} name '{}' is used more than once", type, name);
            key += std::format("#{}", count);
        }
    }

    if (!problem.empty())
    {
        if (m_strictNames)
            return makeError(ErrorCode::InvalidData, std::format("{} (strictNames)", problem));
        problem += "; references follow file order until the parts are renamed";
        log::warn("{}", problem);
        m_warnings.push_back(std::move(problem));
    }
    return key;
}

auto SubAssetKeys::withIndex(std::string_view key, std::size_t index) -> std::string
{
    return std::format("{}/{}", key, index);
}

auto SubAssetKeys::isEntityKey(std::string_view key) -> bool
{
    return key.starts_with(entityType) && key.substr(entityType.size()).starts_with('/');
}

auto SubAssetKeys::typeOf(std::string_view key) -> std::string_view
{
    return key.substr(0, key.find('/'));
}
}
