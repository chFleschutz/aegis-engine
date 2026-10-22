module;

#include <glaze/glaze.hpp>

module aegis.assets;
import std;
import aegis.base;

namespace aegis::assets
{
namespace
{
struct IndexEntryFile
{
    std::string id;
    std::string key;
    std::string type;
};

// One scanned file (a sidecar or native file) with the stamp that tells whether it changed.
struct IndexFileFile
{
    std::uint64_t root{ 0 };
    std::string path; // The scanned file's own path, relative to its root
    std::int64_t modified{ 0 };
    std::uint64_t size{ 0 };
    ImportSettings settings; // A sidecar's settings, which its main entry carries
    std::vector<IndexEntryFile> entries;
};

// Bumped when the shape or meaning changes; an index of another version is rebuilt.
constexpr std::uint32_t indexVersion{ 3 };

struct IndexFile
{
    std::uint32_t version{ indexVersion };
    std::vector<IndexFileFile> files;
};

constexpr glz::opts readOptions{ .error_on_unknown_keys = false };
// Two-space indentation (glaze defaults to three).
struct WriteOptions : glz::opts
{
    std::uint8_t indentation_width = 2;
};
constexpr WriteOptions writeOptions{ { .prettify = true } };
}
}

template<>
struct glz::meta<aegis::assets::ImportSettings>
{
    using T = aegis::assets::ImportSettings;
    static constexpr auto value = object("upAxis", &T::upAxis, "handedness", &T::handedness, "scale", &T::scale,
        "strictNames", &T::strictNames, "srgb", &T::srgb);
};

template<>
struct glz::meta<aegis::assets::IndexEntryFile>
{
    using T = aegis::assets::IndexEntryFile;
    static constexpr auto value = object("id", &T::id, "key", &T::key, "type", &T::type);
};

template<>
struct glz::meta<aegis::assets::IndexFileFile>
{
    using T = aegis::assets::IndexFileFile;
    static constexpr auto value =
        object("root", &T::root, "path", &T::path, "modified", &T::modified, "size", &T::size, "settings", &T::settings,
            "entries", &T::entries);
};

template<>
struct glz::meta<aegis::assets::IndexFile>
{
    using T = aegis::assets::IndexFile;
    static constexpr auto value = object("version", &T::version, "files", &T::files);
};

namespace aegis::assets
{
namespace
{
constexpr std::string_view sidecarExtension{ ".meta" };
constexpr std::string_view cacheFolder{ ".aegis" };

struct FileStamp
{
    std::int64_t modified{ 0 };
    std::uint64_t size{ 0 };
};

auto readFile(const std::filesystem::path& path) -> std::expected<std::string, Error>
{
    std::ifstream stream{ path, std::ios::binary };
    if (!stream)
        return makeError(ErrorCode::FileNotFound, std::format("cannot open '{}'", path.generic_string()));
    return std::string{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
}

auto lowercase(std::string text) -> std::string
{
    for (auto& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

auto isSidecar(const std::filesystem::path& path) -> bool
{
    return path.extension().generic_string() == sidecarExtension;
}

// The path the entries of the file at 'relative' name: a sidecar names its source.
auto sourceOf(const std::string& relative) -> std::string
{
    if (relative.ends_with(sidecarExtension))
        return relative.substr(0, relative.size() - sidecarExtension.size());
    return relative;
}

// The entries a sidecar or native file defines.
auto readEntries(const std::filesystem::path& file, const std::string& relative, std::size_t root)
    -> std::expected<std::vector<AssetEntry>, Error>
{
    auto text = readFile(file);
    if (!text)
        return std::unexpected{ std::move(text.error()) };

    std::vector<AssetEntry> entries;
    if (isSidecar(file))
    {
        auto sidecar = parseSidecar(*text);
        if (!sidecar)
            return makeError(sidecar.error().code, std::format("{}: {}", relative, sidecar.error().message));
        const auto type = typeOfImporter(sidecar->importer);
        if (type.empty())
            return makeError(ErrorCode::InvalidData, std::format("{}: unknown importer '{}'", relative, sidecar->importer));
        const auto source = sourceOf(relative);
        entries.push_back({ sidecar->id, root, source, {}, std::string{ type }, sidecar->settings });
        for (const auto& [key, id] : sidecar->subAssets)
        {
            if (!SubAssetKeys::isEntityKey(key)) // A model's entity IDs, not assets
                entries.push_back({ id, root, source, key, std::string{ SubAssetKeys::typeOf(key) }, {} });
        }
    }
    else
    {
        auto header = parseNativeHeader(*text);
        if (!header)
            return makeError(header.error().code, std::format("{}: {}", relative, header.error().message));
        entries.push_back({ header->id, root, relative, {}, header->type, {} });
    }
    return entries;
}

// Writes a sidecar with a new ID for the foreign file 'source', as the editor does on import.
auto createSidecar(const std::filesystem::path& source) -> std::expected<std::filesystem::path, Error>
{
    const auto meta = sidecarPath(source);
    const auto importer = importerFor(source);
    Sidecar sidecar;
    sidecar.id = AssetId::generate();
    sidecar.importer = std::string{ importer };
    if (importer == "texture")
        sidecar.settings.srgb = true; // Color data is the common case
    if (std::ofstream out{ meta, std::ios::binary })
        out << writeSidecar(sidecar);
    else
        return makeError(ErrorCode::FileNotFound, std::format("cannot write '{}'", meta.generic_string()));
    return meta;
}

// An entry as an error message names it.
auto nameOf(const AssetEntry& entry) -> std::string
{
    return entry.key.empty() ? entry.path : std::format("{} ({})", entry.path, entry.key);
}

// Orders the indexed files by root and path.
constexpr auto byRootAndPath = [](const auto& file) { return std::pair<std::size_t, std::string_view>{ file.root, file.path }; };

auto stampOf(const std::filesystem::path& file) -> FileStamp
{
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(file, ec);
    const auto size = std::filesystem::file_size(file, ec);
    return { static_cast<std::int64_t>(time.time_since_epoch().count()), static_cast<std::uint64_t>(size) };
}

auto loadIndex(const std::filesystem::path& indexFile) -> IndexFile
{
    IndexFile index;
    if (indexFile.empty())
        return index;
    auto text = readFile(indexFile);
    if (!text)
        return index;
    if (const auto result = glz::read<readOptions>(index, *text); result)
    {
        log::warn("Ignoring the asset index '{}': {}", indexFile.generic_string(), glz::format_error(result, *text));
        return IndexFile{};
    }
    if (index.version != indexVersion)
        return IndexFile{};
    return index;
}

// The cached entries of a file, or none if one of them no longer parses.
auto cachedEntries(const IndexFileFile& file, std::size_t root) -> std::optional<std::vector<AssetEntry>>
{
    std::vector<AssetEntry> entries;
    for (const auto& entry : file.entries)
    {
        const auto id = AssetId::parse(entry.id);
        if (!id)
            return std::nullopt;
        entries.push_back({ *id, root, sourceOf(file.path), entry.key, entry.type, entry.key.empty() ? file.settings : ImportSettings{} });
    }
    return entries;
}
}

namespace detail
{
auto findCaseClash(std::span<const std::string> paths) -> std::optional<std::pair<std::string, std::string>>
{
    std::map<std::string, std::string> seen; // Lowercase path -> the path as found
    for (const auto& path : paths)
    {
        const auto [it, inserted] = seen.try_emplace(lowercase(path), path);
        if (!inserted && it->second != path)
            return std::pair{ it->second, path };
    }
    return std::nullopt;
}
}

auto importerFor(const std::filesystem::path& path) -> std::string_view
{
    const auto extension = lowercase(path.extension().generic_string());
    if (extension == ".gltf" || extension == ".glb" || extension == ".obj")
        return "model";
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" || extension == ".bmp")
        return "texture";
    if (extension == ".hdr")
        return "environment";
    return {};
}

auto typeOfImporter(std::string_view importer) -> std::string_view
{
    if (importer == "model")
        return "Scene";
    if (importer == "texture")
        return "Texture";
    if (importer == "environment")
        return "Environment";
    return {};
}

auto isNativeFile(const std::filesystem::path& path) -> bool
{
    const auto extension = path.extension().generic_string();
    return std::ranges::find(AssetDatabase::nativeExtensions, extension) != AssetDatabase::nativeExtensions.end();
}

auto AssetDatabase::scan(ScanDesc desc) -> std::expected<AssetDatabase, Error>
{
    AssetDatabase database;
    database.m_desc = std::move(desc);
    const auto& roots = database.m_desc.roots;

    const auto cached = loadIndex(database.m_desc.indexFile);
    std::map<std::pair<std::uint64_t, std::string>, const IndexFileFile*> cachedFiles;
    for (const auto& file : cached.files)
        cachedFiles.emplace(std::pair{ file.root, file.path }, &file);

    for (std::size_t root = 0; root < roots.size(); ++root)
    {
        const auto& rootPath = roots[root].path;
        std::error_code ec;
        if (!std::filesystem::is_directory(rootPath, ec))
            return makeError(ErrorCode::FileNotFound, std::format("asset root '{}' is not a folder", rootPath.generic_string()));

        std::vector<std::string> allPaths;
        std::vector<std::filesystem::path> files;
        std::vector<std::filesystem::path> sources; // Foreign files that may need a sidecar
        // An error while walking ends the walk, so it fails the scan instead of leaving assets out.
        std::error_code walkError;
        for (auto it = std::filesystem::recursive_directory_iterator{ rootPath, walkError };
            !walkError && it != std::filesystem::recursive_directory_iterator{}; it.increment(walkError))
        {
            if (it->is_directory(ec) && it->path().filename() == cacheFolder)
            {
                it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(ec))
                continue;
            const auto relative = it->path().lexically_relative(rootPath).generic_string();
            allPaths.push_back(relative);
            if (isSidecar(it->path()) || isNativeFile(it->path()))
                files.push_back(it->path());
            else if (database.m_desc.createSidecars && !roots[root].readOnly && !importerFor(it->path()).empty())
                sources.push_back(it->path());
        }
        if (walkError)
            return makeError(ErrorCode::FileNotFound,
                std::format("cannot read the asset root '{}': {}", rootPath.generic_string(), walkError.message()));
        for (const auto& source : sources)
        {
            if (std::filesystem::exists(sidecarPath(source), ec))
                continue;
            auto meta = createSidecar(source);
            if (!meta)
                return std::unexpected{ std::move(meta.error()) };
            allPaths.push_back(meta->lexically_relative(rootPath).generic_string());
            files.push_back(std::move(*meta));
        }
        if (const auto clash = detail::findCaseClash(allPaths))
            return makeError(ErrorCode::InvalidData,
                std::format("'{}' and '{}' differ only in case; paths are case-sensitive", clash->first, clash->second));
        std::ranges::sort(files);

        for (const auto& file : files)
        {
            const auto relative = file.lexically_relative(rootPath).generic_string();
            const auto stamp = stampOf(file);

            std::optional<std::vector<AssetEntry>> entries;
            if (const auto found = cachedFiles.find({ root, relative });
                found != cachedFiles.end() && found->second->modified == stamp.modified && found->second->size == stamp.size)
                entries = cachedEntries(*found->second, root);
            if (!entries)
            {
                auto read = readEntries(file, relative, root);
                if (!read)
                    return std::unexpected{ std::move(read.error()) };
                entries = std::move(*read);
                ++database.m_filesRead;
            }

            IndexedFile record{ root, relative, stamp.modified, stamp.size, {} };
            for (auto& entry : *entries)
            {
                record.entries.push_back(entry.id);
                database.m_entries.push_back(std::move(entry));
            }
            database.m_files.push_back(std::move(record));
        }
    }
    std::ranges::sort(database.m_files, {}, byRootAndPath);

    std::ranges::sort(database.m_entries, {}, &AssetEntry::id);
    for (std::size_t i = 1; i < database.m_entries.size(); ++i)
    {
        const auto& a = database.m_entries[i - 1];
        const auto& b = database.m_entries[i];
        if (a.id == b.id)
            return makeError(ErrorCode::InvalidData, std::format("ID {} is used by both '{}' and '{}'", a.id, nameOf(a), nameOf(b)));
    }

    database.writeIndex();
    return database;
}

auto AssetDatabase::rescan() -> std::expected<void, Error>
{
    auto scanned = scan(m_desc);
    if (!scanned)
        return std::unexpected{ std::move(scanned.error()) };
    *this = std::move(*scanned);
    return {};
}

auto AssetDatabase::addFile(std::size_t root, std::string_view path) -> std::expected<void, Error>
{
    if (root >= m_desc.roots.size())
        return makeError(ErrorCode::InvalidArgument, std::format("there is no asset root {}", root));
    const auto& [rootPath, readOnly] = m_desc.roots[root];
    const std::string source{ path };
    const auto native = isNativeFile(source);
    if (!native && importerFor(source).empty())
        return makeError(ErrorCode::InvalidData, std::format("'{}' is no asset file", source));

    std::error_code ec;
    if (!std::filesystem::is_regular_file(rootPath / source, ec))
        return makeError(ErrorCode::FileNotFound, std::format("'{}' does not exist", source));
    const auto relative = native ? source : source + std::string{ sidecarExtension };
    const auto file = rootPath / relative;
    if (!native && m_desc.createSidecars && !readOnly && !std::filesystem::exists(file, ec))
    {
        if (auto meta = createSidecar(rootPath / source); !meta)
            return std::unexpected{ std::move(meta.error()) };
    }

    std::vector<std::string> paths{ source };
    for (const auto& entry : m_entries)
    {
        if (entry.root == root)
            paths.push_back(entry.path);
    }
    if (const auto clash = detail::findCaseClash(paths))
        return makeError(ErrorCode::InvalidData,
            std::format("'{}' and '{}' differ only in case; paths are case-sensitive", clash->first, clash->second));

    auto entries = readEntries(file, relative, root);
    if (!entries)
        return std::unexpected{ std::move(entries.error()) };

    // The file's earlier entries are replaced, so only an ID of another file clashes.
    const auto isOwn = [&](const AssetEntry& entry) { return entry.root == root && entry.path == source; };
    std::ranges::sort(*entries, {}, &AssetEntry::id);
    for (std::size_t i = 0; i < entries->size(); ++i)
    {
        const auto& entry = (*entries)[i];
        const auto* other = find(entry.id);
        if (other && isOwn(*other))
            other = nullptr;
        if (i > 0 && (*entries)[i - 1].id == entry.id)
            other = &(*entries)[i - 1];
        if (other)
            return makeError(ErrorCode::InvalidData,
                std::format("ID {} of '{}' is already used by '{}'", entry.id, nameOf(entry), nameOf(*other)));
    }

    std::erase_if(m_entries, isOwn);
    const auto stamp = stampOf(file);
    IndexedFile record{ root, relative, stamp.modified, stamp.size, {} };
    for (auto& entry : *entries)
    {
        record.entries.push_back(entry.id);
        m_entries.insert(std::ranges::lower_bound(m_entries, entry.id, {}, &AssetEntry::id), std::move(entry));
    }
    const std::pair<std::size_t, std::string_view> key{ root, relative };
    if (const auto it = std::ranges::lower_bound(m_files, key, {}, byRootAndPath); it != m_files.end() && byRootAndPath(*it) == key)
        *it = std::move(record);
    else
        m_files.insert(it, std::move(record));

    writeIndex();
    return {};
}

auto AssetDatabase::writeIndex() const -> void
{
    const auto& indexFile = m_desc.indexFile;
    if (indexFile.empty())
        return;

    IndexFile index;
    for (const auto& file : m_files)
    {
        IndexFileFile record{ file.root, file.path, file.modified, file.size, {}, {} };
        for (const auto id : file.entries)
        {
            const auto* entry = find(id);
            if (!entry)
                continue;
            if (entry->key.empty())
                record.settings = entry->settings;
            record.entries.push_back({ id.toString(), entry->key, entry->type });
        }
        index.files.push_back(std::move(record));
    }

    std::string json;
    (void)glz::write<writeOptions>(index, json);
    std::error_code ec;
    std::filesystem::create_directories(indexFile.parent_path(), ec);
    if (std::ofstream out{ indexFile, std::ios::binary })
        out << json;
    else
        log::warn("Cannot write the asset index '{}'", indexFile.generic_string());
}

auto AssetDatabase::find(AssetId id) const -> const AssetEntry*
{
    const auto it = std::ranges::lower_bound(m_entries, id, {}, &AssetEntry::id);
    return it != m_entries.end() && it->id == id ? &*it : nullptr;
}

auto AssetDatabase::find(std::size_t root, std::string_view path, std::string_view key) const -> std::optional<AssetId>
{
    for (const auto& entry : m_entries)
    {
        if (entry.root == root && entry.path == path && entry.key == key)
            return entry.id;
    }
    return std::nullopt;
}

auto AssetDatabase::addSubAssets(std::size_t root, std::string_view path, std::span<const std::pair<std::string, AssetId>> parts)
    -> std::expected<void, Error>
{
    for (const auto& [key, id] : parts)
    {
        if (const auto* existing = find(id))
        {
            if (existing->root == root && existing->path == path && existing->key == key)
                continue;
            return makeError(ErrorCode::InvalidData,
                std::format("ID {} for '{}' ({}) is already used by '{}'", id, path, key, existing->path));
        }
        const AssetEntry entry{ id, root, std::string{ path }, key, {}, {} };
        m_entries.insert(std::ranges::lower_bound(m_entries, id, {}, &AssetEntry::id), entry);
    }
    return {};
}

auto AssetDatabase::absolutePath(const AssetEntry& entry) const -> std::filesystem::path
{
    return m_desc.roots[entry.root].path / entry.path;
}
}
