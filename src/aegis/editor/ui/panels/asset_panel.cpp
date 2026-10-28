module;

#include <cfloat>

#include <imgui.h>
#include <imgui_stdlib.h>

module aegis.editor;
import std;
import aegis.asset_library;
import aegis.assets;
import aegis.core;
import aegis.math;
import aegis.serialize;
import aegis.world;
import :panels;

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
            ImGui::TableSetupColumn("Folders", ImGuiTableColumnFlags_WidthStretch, 0.3f);
            ImGui::TableSetupColumn("Files", ImGuiTableColumnFlags_WidthStretch, 0.7f);
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
        ImGui::SetNextItemWidth(160.0f);
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

    // The folder and, while open, its subfolders; a read-only root and its folders are dimmed.
    void drawFolder(std::size_t index)
    {
        const auto& folder = m_tree.folders()[index];
        const bool readOnly = m_tree.roots()[folder.root].readOnly;
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (folder.folders.empty())
            flags |= ImGuiTreeNodeFlags_Leaf;
        if (!folder.parent)
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        if (folder.root == m_root && folder.path == m_folder)
            flags |= ImGuiTreeNodeFlags_Selected;

        if (readOnly)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        const bool open = ImGui::TreeNodeEx(reinterpret_cast<const void*>(index), flags, "%s", folder.name.c_str());
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

    // The open folder's subfolders and files (with a filter, its subfolders' files too), each file with its parts. A
    // double click on a subfolder opens it.
    void drawFiles(EditorContext& editor)
    {
        const auto folder = m_tree.findFolder(m_root, m_folder);
        if (!folder)
            return;
        const auto rows = m_tree.rows(*folder, m_filter);
        if (rows.empty())
            ImGui::TextDisabled(m_filter.active() ? "No matching assets" : "No assets in this folder");

        std::optional<std::size_t> entered;
        for (const auto& row : rows)
        {
            if (row.folder)
            {
                const auto& subfolder = m_tree.folders()[*row.folder];
                const bool dimmed = m_tree.roots()[subfolder.root].readOnly;
                if (dimmed)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::PushID(static_cast<int>(*row.folder));
                ImGui::TreeNodeEx("##folder", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth, "%s/", subfolder.name.c_str());
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    entered = *row.folder;
                ImGui::PopID();
                if (dimmed)
                    ImGui::PopStyleColor();
                continue;
            }

            const auto& file = m_tree.files()[row.file];
            const bool readOnly = m_tree.roots()[file.root].readOnly;
            if (readOnly)
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

            // With a filter the rows come from several folders: show where each is
            const std::string label = m_filter.active() ? file.path : file.name;
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (row.parts.empty())
                flags |= ImGuiTreeNodeFlags_Leaf;
            if (m_filter.active() && !row.parts.empty())
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            ImGui::PushID(static_cast<int>(row.file));
            const bool open = ImGui::TreeNodeEx("##file", flags, "%s", label.c_str());
            if (file.main)
            {
                const auto& main = m_tree.items()[*file.main];
                dragSource(main, file.name);
                if (main.type == serialize::Scene::assetType && file.path.ends_with(".scene") && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    editor.requestOpenScene(main.id);
            }
            if (open)
            {
                for (const auto part : row.parts)
                {
                    const auto& item = m_tree.items()[part];
                    ImGui::PushID(static_cast<int>(part));
                    const auto name = partName(item);
                    ImGui::TreeNodeEx("##part", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", std::format("{}  ({})", name, item.type).c_str());
                    dragSource(item, name);
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
            if (readOnly)
                ImGui::PopStyleColor();
        }
        if (entered)
        {
            m_root = m_tree.folders()[*entered].root;
            m_folder = m_tree.folders()[*entered].path;
        }
    }

    AssetTree m_tree;
    bool m_built{ false };
    std::size_t m_builtSize{ 0 };
    AssetFilter m_filter;
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
    const auto create = [&](world::Entity at) -> std::expected<world::Entity, std::string> {
        auto holder = frame.assets.instantiateScene(*editor.world(), frame.types, assets::AssetId{ asset }, at);
        if (holder && location)
        {
            registry.get<Transform>(*holder).location = *location;
            if (!registry.has<DynamicTag>(*holder))
                registry.markStaticDirty(*holder);
        }
        return holder;
    };
    auto holder = instantiateEntity(editWorld(editor, frame), editor.undoStack(), parent, create);
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
