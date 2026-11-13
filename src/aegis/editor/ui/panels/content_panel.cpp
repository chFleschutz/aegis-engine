module;

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

// The ImGui ID of an asset's line: its asset ID, which survives a rescan.
auto assetId(const AssetItem& item) -> const void*
{
    return reinterpret_cast<const void*>(static_cast<std::uintptr_t>(item.id));
}

// The folder column's width at the start, in pixels at a UI scale of 1.
constexpr float folderColumnWidth = 200.0f;

// A row's padding above and below its label, the gap between rows and the rounding of a row's highlight, in pixels at
// 100%: the hierarchy's.
constexpr float rowPadding{ 3.0f };
constexpr float rowGap{ 1.0f };
constexpr float rowRounding{ 2.0f };

// A tile of the grid view, in pixels at 100%: its width, its icon's size, its padding, the gap between its icon and its
// name, the gap between tiles and the rounding of its highlight.
constexpr float tileWidth{ 88.0f };
constexpr float tileIconSize{ 40.0f };
constexpr float tilePadding{ 6.0f };
constexpr float tileIconGap{ 4.0f };
constexpr float tileSpacing{ 6.0f };
constexpr float tileRounding{ 4.0f };

// The rows' metrics until it goes out of scope: their padding (a tree node's frame padding) and the gap between them.
class RowStyle
{
public:
    RowStyle()
    {
        const float scale = theme::scale();
        const float fontSize = ImGui::GetFontSize();
        const float height = std::round(fontSize + 2.0f * rowPadding * scale);
        const ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ style.ItemSpacing.x, std::round(rowGap * scale) });
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ style.FramePadding.x, (height - fontSize) * 0.5f });
    }
    RowStyle(const RowStyle&) = delete;
    auto operator=(const RowStyle&) -> RowStyle& = delete;
    ~RowStyle() { ImGui::PopStyleVar(2); }
};

// The grid's tiles at the current scale.
struct TileMetrics
{
    float width;
    float height;
    float padding;
    float iconSize;
    float iconGap;
    float spacing;
    float rounding;
};

auto tileMetrics() -> TileMetrics
{
    const float scale = theme::scale();
    const float padding = std::round(tilePadding * scale);
    const float iconSize = std::round(tileIconSize * scale);
    const float iconGap = std::round(tileIconGap * scale);
    return TileMetrics{
        .width = std::round(tileWidth * scale),
        .height = padding + iconSize + iconGap + 2.0f * ImGui::GetFontSize() + padding, // Two lines of name
        .padding = padding,
        .iconSize = iconSize,
        .iconGap = iconGap,
        .spacing = std::round(tileSpacing * scale),
        .rounding = std::round(tileRounding * scale),
    };
}

// 'text' as it fits in 'width': cut short with an ellipsis if it is wider.
auto fitted(std::string_view text, float width) -> std::string
{
    const auto measure = [](std::string_view part) { return ImGui::CalcTextSize(part.data(), part.data() + part.size()).x; };
    if (measure(text) <= width)
        return std::string{ text };
    constexpr std::string_view ellipsis{ "…" };
    const float room = width - measure(ellipsis);
    std::size_t end = text.size();
    while (end > 0 && measure(text.substr(0, end)) > room)
    {
        --end;
        while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) // Not inside a UTF-8 sequence
            --end;
    }
    return std::format("{}{}", text.substr(0, end), ellipsis);
}

// 'text' on at most two lines of 'width': all of it on one if it fits, else broken after the last '_', '-', '.' or space
// that leaves the first line full enough (anywhere without one), the second cut short with an ellipsis if it is wider.
auto twoLines(std::string_view text, float width) -> std::pair<std::string, std::string>
{
    const auto measure = [](std::string_view part) { return ImGui::CalcTextSize(part.data(), part.data() + part.size()).x; };
    if (measure(text) <= width)
        return { std::string{ text }, {} };
    std::size_t end = text.size();
    while (end > 0 && measure(text.substr(0, end)) > width)
    {
        --end;
        while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) // Not inside a UTF-8 sequence
            --end;
    }
    const auto breakAt = text.substr(0, end).find_last_of("_-. ");
    if (breakAt != std::string_view::npos && breakAt + 1 >= end / 2)
        end = breakAt + 1;
    std::string_view rest = text.substr(end);
    while (rest.starts_with(' '))
        rest.remove_prefix(1);
    return { std::string{ text.substr(0, end) }, fitted(rest, width) };
}

// A row of the folder tree or the file list: a tree node with no label of its own, its icon (or 'openIcon' while it is
// open) in 'iconColor' and its name drawn over it, read-only ones muted with the lock after the name, on a highlight
// with rounded corners (ImGui's is square, and clear here). Returns whether the node is open.
auto assetRow(const char* id, ImGuiTreeNodeFlags flags, bool selected, const char* icon, const char* openIcon, ImU32 iconColor, const std::string& name, bool readOnly) -> bool
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->ChannelsSplit(2);
    drawList->ChannelsSetCurrent(1);
    const ImVec4 clear{ 0.0f, 0.0f, 0.0f, 0.0f };
    ImGui::PushStyleColor(ImGuiCol_Header, clear);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, clear);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, clear);
    const ImGuiTreeNodeFlags rowFlags = flags | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth | (selected ? ImGuiTreeNodeFlags_Selected : 0);
    const bool open = ImGui::TreeNodeEx(id, rowFlags, "%s", "");
    ImGui::PopStyleColor(3);

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const ImU32 muted = theme::packed(theme::mutedText(selected));
    ImVec2 position{ min.x + ImGui::GetTreeNodeToLabelSpacing(), min.y + std::round((max.y - min.y - ImGui::GetFontSize()) * 0.5f) };
    const char* shown = open && openIcon ? openIcon : icon;
    drawList->AddText(position, readOnly ? muted : iconColor, shown);
    position.x += ImGui::CalcTextSize(shown).x + spacing;
    if (!readOnly)
    {
        drawList->AddText(position, ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
    }
    else
    {
        // The lock follows the name, or stays at the row's end and cuts a long name short: it must stay in view
        const float lockX = std::min(position.x + ImGui::CalcTextSize(name.c_str()).x + spacing, max.x - ImGui::CalcTextSize(theme::icons::locked).x);
        const ImVec4 clip{ position.x, min.y, lockX - spacing, max.y };
        drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), position, muted, name.c_str(), nullptr, 0.0f, &clip);
        drawList->AddText(ImVec2{ lockX, position.y }, muted, theme::icons::locked);
    }
    if (selected || ImGui::IsItemHovered())
    {
        drawList->ChannelsSetCurrent(0);
        const ImGuiCol highlight = selected ? ImGuiCol_Header : ImGui::IsItemActive() ? ImGuiCol_HeaderActive
                                                                                      : ImGuiCol_HeaderHovered;
        drawList->AddRectFilled(min, max, ImGui::GetColorU32(highlight), std::round(rowRounding * theme::scale()));
    }
    drawList->ChannelsMerge();
    return open;
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
    ImGui::SetItemTooltip("%s", std::format("{}\n{}\n{}", item.key.empty() ? item.path : std::format("{} : {}", item.path, item.key), item.type, assets::AssetId{ item.id }).c_str());
}

// How the browser shows the open folder's contents.
enum class AssetView
{
    List, ///< Rows, a model's parts below it
    Grid, ///< Tiles with large icons, a model's parts after it
};

// The asset files of the project and the engine content: a folder tree, and the open folder's subfolders and files with
// their parts as a list or a grid of tiles, under a breadcrumb of the folder's path. A click selects a line, a double click
// enters a folder, opens a scene file or shows a model's parts. Assets drag onto asset slots, the hierarchy and the
// viewport.
class ContentPanel final : public Panel
{
public:
    ContentPanel() { setOpen(true); }

    [[nodiscard]] auto name() const -> std::string_view override { return "Assets"; }
    [[nodiscard]] auto icon() const -> const char* override { return theme::icons::assets; }
    [[nodiscard]] auto drawer() const -> bool override { return true; }

    void draw(EditorContext& editor, FrameContext& frame) override
    {
        // Importing a model adds its parts to the database, so it changes outside a rescan too
        const auto& database = frame.assets.database();
        if (!m_built || database.size() != m_builtSize)
            rebuild(database);

        drawToolbar(frame);
        if (ImGui::BeginTable("##browser", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
        {
            ImGui::TableSetupColumn("Folders", ImGuiTableColumnFlags_WidthFixed, folderColumnWidth * theme::scale());
            ImGui::TableSetupColumn("Files", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::BeginChild("##folders"))
            {
                const RowStyle rows;
                for (std::size_t root = 0; root < m_tree.roots().size(); ++root)
                    drawFolder(m_tree.rootFolder(root));
            }
            ImGui::EndChild();
            ImGui::TableNextColumn();
            drawBreadcrumb();
            if (ImGui::BeginChild("##files"))
                drawFiles(editor);
            ImGui::EndChild();
            ImGui::EndTable();
        }
    }

private:
    // A line of the file list: a row of m_rows, or one of its file's parts.
    struct FileLine
    {
        std::size_t row{ 0 };
        std::optional<std::size_t> part; // An index into AssetTree::items
    };

    // A line as both views show it.
    struct LineView
    {
        std::string key; // What m_selected holds while it is selected: its root and path, or its asset ID
        const char* icon{ nullptr };
        ImU32 iconColor{ 0 };
        std::string name;
        bool readOnly{ false };            // Engine content
        std::optional<std::size_t> folder; // A subfolder's line: its index into AssetTree::folders
        const AssetFile* file{ nullptr };  // A file's or a part's line: the file
        const AssetItem* item{ nullptr };  // The file's main asset (null without one), or the part
        bool part{ false };                // A part's line
        bool hasParts{ false };            // A file's line that lists parts
    };

    // A file's key for its open state: its root and path, which survive a rebuild.
    using FileKey = std::pair<std::size_t, std::string>;

    [[nodiscard]] static auto keyOf(const AssetFile& file) -> FileKey { return { file.root, file.path }; }

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

    void openFolder(std::size_t index)
    {
        m_root = m_tree.folders()[index].root;
        m_folder = m_tree.folders()[index].path;
    }

    // Rescan, the type filter, the view buttons and the search.
    void drawToolbar(FrameContext& frame)
    {
        if (theme::iconButton(theme::icons::rescan, "rescan"))
        {
            if (auto scanned = frame.assets.rescan(); !scanned)
                log::warn("{}", scanned.error());
            rebuild(frame.assets.database());
        }
        ImGui::SetItemTooltip("Rescan: scan the asset folders again for added, changed and removed files");

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
        if (theme::iconButton(theme::icons::listView, "list", m_view == AssetView::List))
            m_view = AssetView::List;
        ImGui::SetItemTooltip("List");
        ImGui::SameLine(0.0f, 0.0f);
        if (theme::iconButton(theme::icons::gridView, "grid", m_view == AssetView::Grid))
            m_view = AssetView::Grid;
        ImGui::SetItemTooltip("Grid");

        ImGui::SameLine();
        ImGui::SetNextItemWidth(fillWidth());
        searchField("##search", "Search", m_filter.search);
    }

    // The folder and, while open, its subfolders; a read-only root and its folders are muted, the root locked.
    void drawFolder(std::size_t index)
    {
        const auto& folder = m_tree.folders()[index];
        const bool readOnly = m_tree.roots()[folder.root].readOnly;
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
        if (folder.folders.empty())
            flags |= ImGuiTreeNodeFlags_Leaf;
        if (!folder.parent)
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        const bool selected = folder.root == m_root && folder.path == m_folder;

        // A root by what it holds; a folder open while its subfolders show (a leaf is always open to ImGui)
        const char* icon = folder.parent ? theme::icons::folder : readOnly ? theme::icons::engineRoot
                                                                           : theme::icons::projectRoot;
        const char* openIcon = folder.parent && !folder.folders.empty() ? theme::icons::folderOpen : nullptr;
        // Identified by its root and path, so its open state stays with it when a rescan or an import adds folders
        ImGui::PushID(static_cast<int>(folder.root));
        const bool open = assetRow(folder.path.c_str(), flags, selected, icon, openIcon, theme::packed(theme::folderIcon), folder.name, readOnly && !folder.parent);
        ImGui::PopID();
        if (!folder.parent && readOnly)
            ImGui::SetItemTooltip("Engine content (read-only)");
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            openFolder(index);
        if (!open)
            return;
        for (const auto child : folder.folders)
            drawFolder(child);
        ImGui::TreePop();
    }

    // The open folder's path from its root, each folder a button that opens it, after a button up to the parent; with
    // a filter, how many lines match.
    void drawBreadcrumb()
    {
        const auto folder = m_tree.findFolder(m_root, m_folder);
        if (!folder)
            return;
        std::vector<std::size_t> path;
        for (auto at = folder; at; at = m_tree.folders()[*at].parent)
            path.push_back(*at);
        std::ranges::reverse(path);

        ImGui::AlignTextToFramePadding();
        const auto parent = m_tree.folders()[*folder].parent;
        ImGui::BeginDisabled(!parent);
        if (theme::iconButton(theme::icons::up, "up") && parent)
            openFolder(*parent);
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Up to the parent folder");

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });
        for (std::size_t i = 0; i < path.size(); ++i)
        {
            const auto& at = m_tree.folders()[path[i]];
            ImGui::SameLine(0.0f, i == 0 ? ImGui::GetStyle().ItemSpacing.x : 0.0f);
            if (i > 0)
            {
                ImGui::TextColored(theme::textMuted, "%s", theme::icons::collapsed);
                ImGui::SameLine(0.0f, 0.0f);
            }
            const bool last = i + 1 == path.size();
            const char* rootIcon = m_tree.roots()[at.root].readOnly ? theme::icons::engineRoot : theme::icons::projectRoot;
            const std::string label = i == 0 ? std::format("{} {}", rootIcon, at.name) : at.name;
            ImGui::PushStyleColor(ImGuiCol_Text, last ? theme::text : theme::textDim);
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Button(label.c_str()) && !last)
                openFolder(path[i]);
            ImGui::PopID();
            ImGui::PopStyleColor();
        }
        ImGui::PopStyleColor();

        if (m_filter.active())
        {
            const auto count = std::ranges::count_if(cachedLines(*folder), [](const FileLine& line) { return !line.part; });
            ImGui::SameLine();
            ImGui::TextDisabled("%s", std::format("{} {} here and below", count, count == 1 ? "file" : "files").c_str());
        }
    }

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

    [[nodiscard]] auto lineView(const FileLine& line) const -> LineView
    {
        const auto& row = m_rows[line.row];
        if (row.folder)
        {
            const auto& folder = m_tree.folders()[*row.folder];
            return LineView{
                .key = std::format("d{}/{}", folder.root, folder.path),
                .icon = theme::icons::folder,
                .iconColor = theme::packed(theme::folderIcon),
                .name = folder.name,
                .readOnly = m_tree.roots()[folder.root].readOnly,
                .folder = row.folder,
            };
        }
        const auto& file = m_tree.files()[row.file];
        const bool readOnly = m_tree.roots()[file.root].readOnly;
        if (line.part)
        {
            const auto& item = m_tree.items()[*line.part];
            const auto kind = assetKind(item.type, item.path);
            return LineView{
                .key = std::format("a{}", item.id),
                .icon = assetIcon(kind),
                .iconColor = assetIconColor(kind),
                .name = std::string{ partName(item) },
                .readOnly = readOnly,
                .file = &file,
                .item = &item,
                .part = true,
            };
        }
        const AssetItem* main = file.main ? &m_tree.items()[*file.main] : nullptr;
        const auto kind = main ? assetKind(main->type, file.path) : AssetKind::Other;
        // With a filter the lines come from several folders: show where each is
        return LineView{
            .key = std::format("f{}/{}", file.root, file.path),
            .icon = assetIcon(kind),
            .iconColor = assetIconColor(kind),
            .name = m_filter.active() ? file.path : file.name,
            .readOnly = readOnly,
            .file = &file,
            .item = main,
            .hasParts = !row.parts.empty(),
        };
    }

    // A double click on a line: a folder opens, a scene file opens in the editor, a model shows or hides its parts.
    void activate(EditorContext& editor, const LineView& view, std::optional<std::size_t>& entered)
    {
        if (view.folder)
        {
            entered = view.folder;
            return;
        }
        if (view.part || !view.item)
            return;
        if (view.item->type == serialize::Scene::assetType && view.file->path.ends_with(".scene"))
            editor.requestOpenScene(view.item->id);
        else if (view.hasParts)
            setFileOpen(*view.file, !fileOpen(*view.file));
    }

    // The open folder's subfolders and files (with a filter, its subfolders' files too), each open file with its parts,
    // as a list or a grid of which only what is in view is drawn. A click on the free space clears the selection.
    void drawFiles(EditorContext& editor)
    {
        const auto folder = m_tree.findFolder(m_root, m_folder);
        if (!folder)
            return;
        const auto& lines = cachedLines(*folder);
        std::optional<std::size_t> entered;
        if (lines.empty())
            drawCenteredHint(m_filter.active() ? "No matching assets" : "No assets in this folder");
        else if (m_view == AssetView::List)
            drawList(editor, lines, entered);
        else
            drawGrid(editor, lines, entered);
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
            m_selected.clear();
        if (entered)
            openFolder(*entered);
    }

    void drawList(EditorContext& editor, const std::vector<FileLine>& lines, std::optional<std::size_t>& entered)
    {
        const RowStyle rows;
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(lines.size()));
        while (clipper.Step())
        {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
                drawListLine(editor, lines[static_cast<std::size_t>(index)], entered);
        }
    }

    // A line of the list; a file's arrow lists its parts below it, indented.
    void drawListLine(EditorContext& editor, const FileLine& line, std::optional<std::size_t>& entered)
    {
        const auto view = lineView(line);
        const float indent = ImGui::GetStyle().IndentSpacing;
        if (view.part)
            ImGui::Indent(indent);
        ImGui::PushID(view.key.c_str());
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (!view.hasParts)
            flags |= ImGuiTreeNodeFlags_Leaf;
        const bool wasOpen = view.hasParts && fileOpen(*view.file);
        if (view.hasParts)
            ImGui::SetNextItemOpen(wasOpen);
        const bool open = assetRow("##line", flags, m_selected == view.key, view.icon, nullptr, view.iconColor, view.name, view.readOnly);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            m_selected = view.key;
        if (view.item)
            dragSource(*view.item, view.name);
        const bool onLabel = ImGui::GetMousePos().x >= ImGui::GetItemRectMin().x + ImGui::GetTreeNodeToLabelSpacing();
        if (onLabel && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            activate(editor, view, entered);
        ImGui::PopID();
        if (view.part)
            ImGui::Unindent(indent);
        if (view.hasParts && open != wasOpen) // The parts follow from the next frame
            setFileOpen(*view.file, open);
    }

    // The lines as tiles, as many to a row as fit.
    void drawGrid(EditorContext& editor, const std::vector<FileLine>& lines, std::optional<std::size_t>& entered)
    {
        const TileMetrics tile = tileMetrics();
        const int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + tile.spacing) / (tile.width + tile.spacing)));
        const int count = static_cast<int>(lines.size());
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ tile.spacing, tile.spacing });
        ImGuiListClipper clipper;
        clipper.Begin((count + columns - 1) / columns, tile.height + tile.spacing);
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                for (int column = 0; column < columns && row * columns + column < count; ++column)
                {
                    if (column > 0)
                        ImGui::SameLine();
                    drawTile(editor, lineView(lines[static_cast<std::size_t>(row * columns + column)]), tile, entered);
                }
            }
        }
        ImGui::PopStyleVar();
    }

    // A tile: the icon large over the name on up to two lines, cut short to fit (the tooltip has it whole), the lock in the corner for engine
    // content. A part's tile has a fill, so the parts after their model stand out; a model's tile has a button at its right
    // edge that shows its parts and hides them.
    void drawTile(EditorContext& editor, const LineView& view, const TileMetrics& tile, std::optional<std::size_t>& entered)
    {
        ImGui::PushID(view.key.c_str());
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max{ min.x + tile.width, min.y + tile.height };
        ImGui::SetNextItemAllowOverlap(); // The parts button goes over it
        ImGui::InvisibleButton("##tile", ImVec2{ tile.width, tile.height });
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        if (ImGui::IsItemClicked())
            m_selected = view.key;
        if (view.item)
            dragSource(*view.item, view.name);
        else
            ImGui::SetItemTooltip("%s", view.name.c_str());
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            activate(editor, view, entered);

        const bool selected = m_selected == view.key;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (selected || hovered || view.part)
        {
            const ImVec4 fill = selected ? theme::selection : active ? theme::bgFrameActive
                                                          : hovered  ? theme::bgFrameHover
                                                                     : theme::bgFrame;
            drawList->AddRectFilled(min, max, theme::packed(fill), tile.rounding);
        }
        const ImU32 muted = theme::packed(theme::mutedText(selected));
        drawList->AddText(ImGui::GetFont(), tile.iconSize, ImVec2{ min.x + std::round((tile.width - tile.iconSize) * 0.5f), min.y + tile.padding }, view.readOnly ? muted : view.iconColor, view.icon);
        const auto [first, second] = twoLines(view.name, tile.width - 2.0f * tile.padding);
        const ImU32 nameColor = view.readOnly ? muted : ImGui::GetColorU32(ImGuiCol_Text);
        float lineY = min.y + tile.padding + tile.iconSize + tile.iconGap;
        for (const auto& line : { first, second })
        {
            const float lineWidth = ImGui::CalcTextSize(line.c_str()).x;
            drawList->AddText(ImVec2{ std::round(min.x + (tile.width - lineWidth) * 0.5f), lineY }, nameColor, line.c_str());
            lineY += ImGui::GetFontSize();
        }
        if (view.readOnly)
            drawList->AddText(ImVec2{ max.x - tile.padding - ImGui::CalcTextSize(theme::icons::locked).x, min.y + tile.padding }, muted, theme::icons::locked);

        if (view.hasParts)
        {
            const bool open = fileOpen(*view.file);
            const float fontSize = ImGui::GetFontSize();
            const ImVec2 size{ fontSize + 2.0f, std::round(fontSize * 1.6f) };
            const ImVec2 buttonMin{ max.x - size.x - std::round(tile.padding * 0.5f), min.y + tile.padding + std::round((tile.iconSize - size.y) * 0.5f) };
            ImGui::SetCursorScreenPos(buttonMin);
            if (ImGui::InvisibleButton("##parts", size))
                setFileOpen(*view.file, !open);
            ImGui::SetItemTooltip("%s", open ? "Hide the parts" : std::format("Show the {} parts", view.file->parts.size()).c_str());
            if (ImGui::IsItemHovered())
                drawList->AddRectFilled(buttonMin, ImVec2{ buttonMin.x + size.x, buttonMin.y + size.y }, theme::packed(theme::bgFrameActive), std::round(tile.rounding * 0.5f));
            const char* chevron = open ? theme::icons::hideParts : theme::icons::showParts;
            drawList->AddText(ImVec2{ std::round(buttonMin.x + (size.x - ImGui::CalcTextSize(chevron).x) * 0.5f), std::round(buttonMin.y + (size.y - fontSize) * 0.5f) }, ImGui::GetColorU32(ImGuiCol_Text), chevron);
            // The tile is the line's item again, so the next tile follows it
            ImGui::SetCursorScreenPos(min);
            ImGui::Dummy(ImVec2{ tile.width, tile.height });
        }
        ImGui::PopID();
    }

    AssetTree m_tree;
    std::uint64_t m_treeVersion{ 0 }; // Counts the rebuilds of m_tree
    bool m_built{ false };
    std::size_t m_builtSize{ 0 };
    AssetFilter m_filter;
    AssetView m_view{ AssetView::List };
    std::string m_selected; // The selected line's LineView::key; empty for none
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

auto makeContentPanel() -> std::unique_ptr<Panel>
{
    return std::make_unique<ContentPanel>();
}
}
