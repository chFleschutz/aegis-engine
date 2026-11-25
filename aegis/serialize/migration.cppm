export module aegis.serialize:migration;
import std;
import aegis.reflect;
import :error;

// A native file type's migration chain (docs/asset-system.md, "Native files"): one upgrade per version, from n to
// n + 1, run on load until the file is at the type's current version, so an older file reads like a new one and
// the next save writes the current version. A component's own fields upgrade through its reflect::TypeInfo
// instead; a chain here edits what the file type itself owns (its keys and their shape).
export namespace aegis::serialize
{
// Upgrades a whole file, header included, from one version to the next. Its errors are returned as they are.
using FileMigration = std::function<std::expected<void, Error>(reflect::Node& root)>;

// A file type's chain: the step at key n upgrades version n to n + 1.
using FileMigrations = std::map<std::uint32_t, FileMigration>;

// Brings 'root', a file of 'type' saved at version 'from', up to 'current', one step of 'migrations' at a time,
// and sets its "version" to 'current'. Nothing runs for a file at 'current' already.
// Fails with UnsupportedVersion for a version newer than 'current' or a missing step; a failing step's error comes
// back as it is, so 'root' may be half upgraded then.
[[nodiscard]] auto upgradeFile(reflect::Node& root, std::string_view type, std::uint32_t from, std::uint32_t current,
    const FileMigrations& migrations) -> std::expected<void, Error>;
}
