module;

#include <cfloat>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "aegis/editor/ui/theme.h"

module aegis.editor;
import std;
import aegis.asset_library;
import aegis.assets;
import aegis.core;
import aegis.math;
import aegis.reflect;
import aegis.serialize;
import aegis.world;
import :panels;
import :theme;

namespace aegis::editor
{
namespace
{
// The browser's items: the database's entries as plain values, its roots by name.
auto buildTree(const assets::AssetDatabase& database) -> AssetTree
{
    std::vector<AssetItem> items;
    items.reserve(database.size());
    for (const auto& entry : database.entries())
        items.push_back(AssetItem{ .id = entry.id.value, .root = entry.root, .path = entry.path, .key = entry.key, .type = entry.type });
    std::vector<AssetRoot> roots;
    for (const auto& root : database.roots())
        roots.push_back(AssetRoot{ .name = root.readOnly ? "Engine" : "Project", .readOnly = root.readOnly });
    return AssetTree::build(std::move(items), std::move(roots));
}

// A part's name in the list: its key without the type prefix ("Helmet/0" for "StaticMesh/Helmet/0").
auto partName(const AssetItem& item) -> std::string_view
{
    std::string_view key = item.key;
    if (key.starts_with(item.type) && key.size() > item.type.size() && key[item.type.size()] == '/')
        key.remove_prefix(item.type.size() + 1);
    return key;
}

// The icon of an asset kind.
auto kindIcon(AssetKind kind) -> const char*
{
    switch (kind)
    {
    case AssetKind::StaticMesh:
        return theme::icons::mesh;
    case AssetKind::Material:
        return theme::icons::material;
    case AssetKind::Texture:
        return theme::icons::texture;
    case AssetKind::Scene:
        return theme::icons::scene;
    case AssetKind::Environment:
        return theme::icons::environment;
    case AssetKind::Model:
        return theme::icons::model;
    case AssetKind::Other:
        return theme::icons::file;
    }
    return theme::icons::file;
}

// The ImGui ID of an asset's line: its asset ID, which survives a rescan.
auto assetId(const AssetItem& item) -> const void*
{
    return reinterpret_cast<const void*>(static_cast<std::uintptr_t>(item.id));
}

// The folder column's width at the start, in pixels at a UI scale of 1.
constexpr float folderColumnWidth = 200.0f;

// The label of a line, which ends in the lock when it is read-only (engine content).
auto lineLabel(std::string label, bool readOnly) -> std::string
{
    if (readOnly)
        label += std::format("  {}", theme::icons::locked);
    return label;
}

// The item just drawn as a drag source of 'item', and its tooltip.
void dragSource(const AssetItem& item, std::string_view label)
{
    if (ImGui::BeginDragDropSource())
    {
        const auto payload = makeAssetPayload(item.id, item.type);
        ImGui::SetDragDropPayload(assetPayloadType, &payload, sizeof(payload));
        ImGui::TextUnformatted(std::format("{} ({})", label, item.type).c_str());
        ImGui::EndDragDropSource();
    }
    ImGui::SetItemTooltip("%s", std::format("{}\n{}", item.type, assets::AssetId{ item.id }).c_str());
}

// The asset files of the project and the engine content: a folder tree, and the open folder's files with their parts.
// Assets drag onto asset slots, the hierarchy and the viewport; a double click on a scene file opens it.
class AssetPanel final : public Panel
{
public:
    AssetPanel() { setOpen(true); }

    [[nodiscard]] auto name() const -> std::string_view override { return "Assets"; }

    void draw(EditorContext& editor, FrameContext& frame) override
    {
        bool open = isOpen();
        if (!ImGui::Begin("Assets", &open))
        {
            ImGui::End();
            setOpen(open);
            return;
        }
        setOpen(open);

        // Importing a model adds its parts to the database, so it changes outside a rescan too
        const auto& database = frame.assets.database();
        if (!m_built || database.size() != m_builtSize)
            rebuild(database);

        drawToolbar(frame);
        if (ImGui::BeginTable("##browser", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
        {
            const ImGuiStyle& style = ImGui::GetStyle();
            ImGui::TableSetupColumn("Folders", ImGuiTableColumnFlags_WidthFixed, folderColumnWidth * style.FontScaleDpi * style.FontScaleMain);
            ImGui::TableSetupColumn("Files", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::BeginChild("##folders"))
            {
                for (std::size_t root = 0; root < m_tree.roots().size(); ++root)
                    drawFolder(m_tree.rootFolder(root));
            }
            ImGui::EndChild();
            ImGui::TableNextColumn();
            if (ImGui::BeginChild("##files"))
                drawFiles(editor);
            ImGui::EndChild();
            ImGui::EndTable();
        }
        ImGui::End();
    }

private:
    void rebuild(const assets::AssetDatabase& database)
    {
        m_tree = buildTree(database);
        ++m_treeVersion; // The cached lines index the old tree
        m_built = true;
        m_builtSize = database.size();
        if (!m_tree.findFolder(m_root, m_folder))
        {
            m_root = 0;
            m_folder.clear();
        }
        // A type that is gone shows nothing
        const auto types = m_tree.types();
        if (!m_filter.type.empty() && std::ranges::find(types, m_filter.type) == types.end())
            m_filter.type.clear();
    }

    void drawToolbar(FrameContext& frame)
    {
        if (ImGui::Button("Rescan"))
        {
            if (auto scanned = frame.assets.rescan(); !scanned)
                log::warn("{}", scanned.error());
            rebuild(frame.assets.database());
        }
        ImGui::SetItemTooltip("Scan the asset folders again for added, changed and removed files");

        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.0f * theme::scale());
        if (ImGui::BeginCombo("##type", m_filter.type.empty() ? "All types" : m_filter.type.c_str()))
        {
            if (ImGui::Selectable("All types", m_filter.type.empty()))
                m_filter.type.clear();
            for (const auto& type : m_tree.types())
            {
                if (ImGui::Selectable(type.c_str(), type == m_filter.type))
                    m_filter.type = type;
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##search", "Search", &m_filter.search);
    }

    // The folder and, while open, its subfolders; a read-only root and its folders are muted and locked.
    void drawFolder(std::size_t index)
    {
        const auto& folder = m_tree.folders()[index];
        const bool readOnly = m_tree.roots()[folder.root].readOnly;
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (folder.folders.empty())
            flags |= ImGuiTreeNodeFlags_Leaf;
        if (!folder.parent)
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        const bool selected = folder.root == m_root && folder.path == m_folder;
        if (selected)
            flags |= ImGuiTreeNodeFlags_Selected;

        if (readOnly)
            ImGui::PushStyleColor(ImGuiCol_Text, theme::mutedText(selected));
        // Identified by its root and path, so its open state stays with it when a rescan or an import adds folders
        ImGui::PushID(static_cast<int>(folder.root));
        const bool open = ImGui::TreeNodeEx(folder.path.c_str(), flags, "%s", lineLabel(folder.name, readOnly).c_str());
        ImGui::PopID();
        if (readOnly)
            ImGui::PopStyleColor();
        if (!folder.parent && readOnly)
            ImGui::SetItemTooltip("Engine content (read-only)");
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            m_root = folder.root;
            m_folder = folder.path;
        }
        if (!open)
            return;
        for (const auto child : folder.folders)
            drawFolder(child);
        ImGui::TreePop();
    }

    // A line of the file list: a row of m_rows, or one of its file's parts.
    struct FileLine
    {
        std::size_t row{ 0 };
        std::optional<std::size_t> part; // An index into AssetTree::items
    };

    // A file's key for its open state: its root and path, which survive a rebuild.
    using FileKey = std::pair<std::size_t, std::string>;

    [[nodiscard]] static auto keyOf(const AssetFile& file) -> FileKey { return { file.root, file.path }; }

    // Whether a file's parts are listed below it: closed by default, but open while filtering (the parts that match are
    // why the file is listed), each with a state of its own.
    [[nodiscard]] auto fileOpen(const AssetFile& file) const -> bool
    {
        return m_filter.active() ? !m_closedWhileFiltered.contains(keyOf(file)) : m_openFiles.contains(keyOf(file));
    }

    // Without a filter the open files are remembered, with one the closed ones.
    void setFileOpen(const AssetFile& file, bool open)
    {
        const bool filtered = m_filter.active();
        auto& files = filtered ? m_closedWhileFiltered : m_openFiles;
        if (open != filtered)
            files.insert(keyOf(file));
        else
            files.erase(keyOf(file));
        m_linesDirty = true;
    }

    // The lines of the open folder, rebuilt only when the tree, the folder, the filter or an open file changed.
    auto cachedLines(std::size_t folder) -> const std::vector<FileLine>&
    {
        if (!m_linesDirty && m_linesTreeVersion == m_treeVersion && m_linesFolder == folder && m_linesFilter == m_filter)
            return m_lines;
        m_rows = m_tree.rows(folder, m_filter);
        m_lines.clear();
        for (std::size_t row = 0; row < m_rows.size(); ++row)
        {
            m_lines.push_back({ .row = row });
            if (m_rows[row].folder || !fileOpen(m_tree.files()[m_rows[row].file]))
                continue;
            for (const auto part : m_rows[row].parts)
                m_lines.push_back({ .row = row, .part = part });
        }
        m_linesTreeVersion = m_treeVersion;
        m_linesFolder = folder;
        m_linesFilter = m_filter;
        m_linesDirty = false;
        return m_lines;
    }

    // The open folder's subfolders and files (with a filter, its subfolders' files too), each open file with its parts,
    // as flat lines of which only those in view are drawn. A double click on a subfolder opens it.
    void drawFiles(EditorContext& editor)
    {
        const auto folder = m_tree.findFolder(m_root, m_folder);
        if (!folder)
            return;
        const auto& lines = cachedLines(*folder);
        if (lines.empty())
            ImGui::TextDisabled(m_filter.active() ? "No matching assets" : "No assets in this folder");

        std::optional<std::size_t> entered;
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(lines.size()));
        while (clipper.Step())
        {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
            {
                const auto& line = lines[static_cast<std::size_t>(index)];
                const auto& row = m_rows[line.row];
                if (row.folder)
                    drawSubfolder(*row.folder, entered);
                else if (line.part)
                    drawPart(m_tree.files()[row.file], *line.part);
                else
                    drawFile(editor, row);
            }
        }
        if (entered)
        {
            m_root = m_tree.folders()[*entered].root;
            m_folder = m_tree.folders()[*entered].path;
        }
    }

    // A subfolder's line, identified by its path; a double click enters it.
    void drawSubfolder(std::size_t index, std::optional<std::size_t>& entered)
    {
        const auto& subfolder = m_tree.folders()[index];
        const bool readOnly = m_tree.roots()[subfolder.root].readOnly;
        if (readOnly)
            ImGui::PushStyleColor(ImGuiCol_Text, theme::textMuted);
        ImGui::PushID(subfolder.path.c_str());
        ImGui::TreeNodeEx("##folder", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", lineLabel(subfolder.name + "/", readOnly).c_str());
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            entered = index;
        ImGui::PopID();
        if (readOnly)
            ImGui::PopStyleColor();
    }

    // A file's line, identified by its main asset's ID (by its path without one); its arrow lists its parts below it.
    void drawFile(EditorContext& editor, const AssetRow& row)
    {
        const auto& file = m_tree.files()[row.file];
        const bool readOnly = m_tree.roots()[file.root].readOnly;
        if (readOnly)
            ImGui::PushStyleColor(ImGuiCol_Text, theme::textMuted);

        // With a filter the rows come from several folders: show where each is
        const auto kind = file.main ? assetKind(m_tree.items()[*file.main].type, file.path) : AssetKind::Other;
        const std::string label = lineLabel(std::format("{}  {}", kindIcon(kind), m_filter.active() ? file.path : file.name), readOnly);
        const bool hasParts = !row.parts.empty();
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (!hasParts)
            flags |= ImGuiTreeNodeFlags_Leaf;
        const bool wasOpen = hasParts && fileOpen(file);
        if (hasParts)
            ImGui::SetNextItemOpen(wasOpen);
        if (file.main)
            ImGui::PushID(assetId(m_tree.items()[*file.main]));
        else
            ImGui::PushID(file.path.c_str());
        const bool open = ImGui::TreeNodeEx("##file", flags, "%s", label.c_str());
        if (file.main)
        {
            const auto& main = m_tree.items()[*file.main];
            dragSource(main, file.name);
            if (main.type == serialize::Scene::assetType && file.path.ends_with(".scene") && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                editor.requestOpenScene(main.id);
        }
        ImGui::PopID();
        if (readOnly)
            ImGui::PopStyleColor();
        if (hasParts && open != wasOpen) // The parts follow from the next frame
            setFileOpen(file, open);
    }

    // A part's line, indented below its file and identified by its asset ID.
    void drawPart(const AssetFile& file, std::size_t part)
    {
        const auto& item = m_tree.items()[part];
        const bool readOnly = m_tree.roots()[file.root].readOnly;
        if (readOnly)
            ImGui::PushStyleColor(ImGuiCol_Text, theme::textMuted);
        const float indent = ImGui::GetStyle().IndentSpacing;
        ImGui::Indent(indent);
        ImGui::PushID(assetId(item));
        const auto name = partName(item);
        ImGui::TreeNodeEx("##part", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", lineLabel(std::format("{}  {}  ({})", kindIcon(assetKind(item.type, item.path)), name, item.type), readOnly).c_str());
        dragSource(item, name);
        ImGui::PopID();
        ImGui::Unindent(indent);
        if (readOnly)
            ImGui::PopStyleColor();
    }

    AssetTree m_tree;
    std::uint64_t m_treeVersion{ 0 }; // Counts the rebuilds of m_tree
    bool m_built{ false };
    std::size_t m_builtSize{ 0 };
    AssetFilter m_filter;
    // The file list, see cachedLines
    std::vector<AssetRow> m_rows;
    std::vector<FileLine> m_lines;
    std::uint64_t m_linesTreeVersion{ 0 };
    std::size_t m_linesFolder{ 0 };
    AssetFilter m_linesFilter;
    bool m_linesDirty{ true };
    std::set<FileKey> m_openFiles;           // Without a filter: the files whose parts are listed
    std::set<FileKey> m_closedWhileFiltered; // With one: the files whose parts are not
    // The open folder, by root and path so it survives a rebuild
    std::size_t m_root{ 0 };
    std::string m_folder;
};
}

auto acceptAssetPayload(const std::function<bool(const AssetPayload& payload)>& accepts) -> std::optional<AssetPayload>
{
    const ImGuiPayload* active = ImGui::GetDragDropPayload();
    if (!active || !active->IsDataType(assetPayloadType) || active->DataSize != static_cast<int>(sizeof(AssetPayload)))
        return std::nullopt;
    AssetPayload payload;
    std::memcpy(&payload, active->Data, sizeof(payload));
    if (!accepts(payload))
    {
        ImGui::SetTooltip("%s", std::format("A {} cannot go here", payload.assetType()).c_str());
        return std::nullopt;
    }
    return ImGui::AcceptDragDropPayload(assetPayloadType) ? std::optional{ payload } : std::nullopt;
}

auto instantiateAsset(EditorContext& editor, FrameContext& frame, std::uint64_t asset, world::Entity parent, std::optional<glm::vec3> location) -> bool
{
    auto& registry = editor.world()->registry();
    const auto edits = editWorld(editor, frame);
    const auto create = [&](world::Entity at) -> std::expected<world::Entity, std::string> {
        auto holder = frame.assets.instantiateScene(*editor.world(), frame.types, assets::AssetId{ asset }, at);
        const auto* transformType = frame.types.find<Transform>();
        if (holder && location && transformType)
        {
            const FieldValues values{ { .field = "location", .value = reflect::Value{ *location } } };
            if (auto written = writeComponent(*transformType, registry, *holder, &registry.get<Transform>(*holder), values, edits.prepare); !written)
                log::warn("Cannot place the instance: {}", reflect::describe(written.error()));
        }
        return holder;
    };
    auto holder = instantiateEntity(edits, editor.undoStack(), parent, create);
    if (!holder)
    {
        log::warn("Cannot instantiate {}: {}", assets::AssetId{ asset }, holder.error());
        return false;
    }
    editor.select(*holder);
    return true;
}

auto makeAssetPanel() -> std::unique_ptr<Panel>
{
    return std::make_unique<AssetPanel>();
}
}
