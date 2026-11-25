module aegis.serialize;
import std;
import aegis.reflect;

namespace aegis::serialize
{
auto upgradeFile(reflect::Node& root, std::string_view type, std::uint32_t from, std::uint32_t current, const FileMigrations& migrations)
    -> std::expected<void, Error>
{
    reflect::Node* version = root.find("version");
    const std::uint32_t line = version ? version->line : root.line;
    if (from > current)
        return makeError(ErrorCode::UnsupportedVersion, std::format("the {} is version {}, newer than this engine's {}", type, from, current), line);
    if (from == 0)
        return makeError(ErrorCode::UnsupportedVersion, std::format("the {} has no version 0", type), line);

    for (std::uint32_t step = from; step < current; ++step)
    {
        auto migration = migrations.find(step);
        if (migration == migrations.end())
            return makeError(ErrorCode::UnsupportedVersion, std::format("the {} has no upgrade from version {}", type, step), line);
        if (auto migrated = migration->second(root); !migrated)
            return migrated;
    }
    if (from != current)
        root.set("version", reflect::Node::makeNumber(std::to_string(current)));
    return {};
}
}
