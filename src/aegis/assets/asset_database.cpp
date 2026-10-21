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
    std::vector<IndexEntryFile> entries;
};

struct IndexFile
{
    std::uint32_t version{ 1 };
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
        object("root", &T::root, "path", &T::path, "modified", &T::modified, "size", &T::size, "entries", &T::entries);
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
        const auto source = sourceOf(relative);
        entries.push_back({ sidecar->id, root, source, {}, {} });
        for (const auto& [key, id] : sidecar->subAssets)
            entries.push_back({ id, root, source, key, {} });
    }
    else
    {
        auto header = parseNativeHeader(*text);
        if (!header)
            return makeError(header.error().code, std::format("{}: {}", relative, header.error().message));
        entries.push_back({ header->id, root, relative, {}, header->type });
    }
    return entries;
}

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
        entries.push_back({ *id, root, sourceOf(file.path), entry.key, entry.type });
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
    auto extension = path.extension().generic_string();
    for (auto& c : extension)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (extension == ".gltf" || extension == ".glb" || extension == ".obj")
        return "model";
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" || extension == ".bmp")
        return "texture";
    if (extension == ".hdr")
        return "environment";
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
    database.m_roots = std::move(desc.roots);

    const auto cached = loadIndex(desc.indexFile);
    std::map<std::pair<std::uint64_t, std::string>, const IndexFileFile*> cachedFiles;
    for (const auto& file : cached.files)
        cachedFiles.emplace(std::pair{ file.root, file.path }, &file);

    IndexFile index;
    for (std::size_t root = 0; root < database.m_roots.size(); ++root)
    {
        const auto& rootPath = database.m_roots[root].path;
        std::error_code ec;
        if (!std::filesystem::is_directory(rootPath, ec))
            return makeError(ErrorCode::FileNotFound, std::format("asset root '{}' is not a folder", rootPath.generic_string()));

        std::vector<std::string> allPaths;
        std::vector<std::filesystem::path> files;
        std::vector<std::filesystem::path> sources; // Foreign files that may need a sidecar
        for (auto it = std::filesystem::recursive_directory_iterator{ rootPath, ec };
            it != std::filesystem::recursive_directory_iterator{}; it.increment(ec))
        {
            if (it->is_directory(ec) && it->path().filename() == cacheFolder)
            {
                it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(ec))
                continue;
            const auto relative = std::filesystem::relative(it->path(), rootPath).generic_string();
            allPaths.push_back(relative);
            if (isSidecar(it->path()) || isNativeFile(it->path()))
                files.push_back(it->path());
            else if (desc.createSidecars && !database.m_roots[root].readOnly && !importerFor(it->path()).empty())
                sources.push_back(it->path());
        }
        for (const auto& source : sources)
        {
            const auto meta = sidecarPath(source);
            if (std::filesystem::exists(meta, ec))
                continue;
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
            allPaths.push_back(std::filesystem::relative(meta, rootPath).generic_string());
            files.push_back(meta);
        }
        if (const auto clash = detail::findCaseClash(allPaths))
            return makeError(ErrorCode::InvalidData,
                std::format("'{}' and '{}' differ only in case; paths are case-sensitive", clash->first, clash->second));
        std::ranges::sort(files);

        for (const auto& file : files)
        {
            const auto relative = std::filesystem::relative(file, rootPath).generic_string();
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

            IndexFileFile record{ root, relative, stamp.modified, stamp.size, {} };
            for (auto& entry : *entries)
            {
                record.entries.push_back({ entry.id.toString(), entry.key, entry.type });
                database.m_entries.push_back(std::move(entry));
            }
            index.files.push_back(std::move(record));
        }
    }

    std::ranges::sort(database.m_entries, {}, &AssetEntry::id);
    for (std::size_t i = 1; i < database.m_entries.size(); ++i)
    {
        const auto& a = database.m_entries[i - 1];
        const auto& b = database.m_entries[i];
        if (a.id == b.id)
        {
            const auto name = [](const AssetEntry& e) { return e.key.empty() ? e.path : std::format("{} ({})", e.path, e.key); };
            return makeError(ErrorCode::InvalidData, std::format("ID {} is used by both '{}' and '{}'", a.id, name(a), name(b)));
        }
    }

    if (!desc.indexFile.empty())
    {
        std::string json;
        (void)glz::write<writeOptions>(index, json);
        std::error_code ec;
        std::filesystem::create_directories(desc.indexFile.parent_path(), ec);
        if (std::ofstream out{ desc.indexFile, std::ios::binary })
            out << json;
        else
            log::warn("Cannot write the asset index '{}'", desc.indexFile.generic_string());
    }
    return database;
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
        const AssetEntry entry{ id, root, std::string{ path }, key, {} };
        m_entries.insert(std::ranges::lower_bound(m_entries, id, {}, &AssetEntry::id), entry);
    }
    return {};
}

auto AssetDatabase::absolutePath(const AssetEntry& entry) const -> std::filesystem::path
{
    return m_roots[entry.root].path / entry.path;
}
}
