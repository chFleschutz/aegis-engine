module;

#include <cfloat>

#include <imgui.h>

#include "aegis/editor/theme.h"

module aegis.editor;
import std;
import aegis.asset_library;
import aegis.assets;
import aegis.core;
import aegis.math;
import aegis.reflect;
import aegis.serialize;
import :edits;
import :panels;
import :theme;
import :widgets;

namespace aegis::editor
{
namespace
{
// The parameters of 'materialTemplate' as the panel shows them, in the order of its parameter block.
auto templateParameters(const assets::MaterialTemplate& materialTemplate) -> std::vector<MaterialParameterInfo>
{
    std::vector<std::pair<std::size_t, MaterialParameterInfo>> ordered;
    for (const auto& [name, parameter] : materialTemplate.parameters())
    {
        MaterialParameterInfo info{ .name = name };
        std::visit(
            [&info](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::same_as<T, std::int32_t>)
                {
                    info.kind = reflect::FieldKind::Int;
                    info.defaultValue = std::int64_t{ value };
                }
                else if constexpr (std::same_as<T, std::uint32_t>)
                {
                    info.kind = reflect::FieldKind::UInt;
                    info.defaultValue = std::uint64_t{ value };
                }
                else if constexpr (std::same_as<T, float>)
                {
                    info.kind = reflect::FieldKind::Float;
                    info.defaultValue = value;
                }
                else if constexpr (std::same_as<T, math::Float2>)
                {
                    info.kind = reflect::FieldKind::Vec2;
                    info.defaultValue = value;
                }
                else if constexpr (std::same_as<T, math::Float3>)
                {
                    info.kind = reflect::FieldKind::Vec3;
                    info.defaultValue = value;
                }
                else if constexpr (std::same_as<T, math::Float4>)
                {
                    info.kind = reflect::FieldKind::Vec4;
                    info.defaultValue = value;
                }
                else
                {
                    info.kind = reflect::FieldKind::AssetRef;
                    info.defaultValue = std::uint64_t{ 0 }; // The template's solid colour
                }
            },
            parameter.defaultValue);
        ordered.emplace_back(parameter.offset, std::move(info));
    }
    std::ranges::sort(ordered, {}, &std::pair<std::size_t, MaterialParameterInfo>::first);
    std::vector<MaterialParameterInfo> parameters;
    for (auto& [offset, info] : ordered)
        parameters.push_back(std::move(info));
    return parameters;
}

auto readText(const std::filesystem::path& file) -> std::optional<std::string>
{
    std::ifstream stream{ file, std::ios::binary };
    if (!stream)
        return std::nullopt;
    return std::string{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
}

// The label column's share of the width, as the inspector's: at least, and at most for a long label.
constexpr float labelMinShare{ 0.3f };
constexpr float labelMaxShare{ 0.4f };

// One material file (`.material`) at a time, opened by a double click in the content browser or New Material: a row
// per parameter of its template, with the file's value or the template's default. A parameter the file sets has its
// label in the accent colour and a reset button, which removes it from the file (as does a texture of None). Edits
// change the loaded material at once, are undone with the scene's edits and are written by Save.
class MaterialPanel final : public Panel
{
public:
    [[nodiscard]] auto name() const -> std::string_view override { return panels::material; }
    [[nodiscard]] auto icon() const -> const char* override { return theme::icons::material; }
    [[nodiscard]] auto placement() const -> Placement override { return Placement::Float; }

    void draw(EditorContext& editor, FrameContext& frame) override
    {
        if (const auto request = editor.takeMaterialRequest())
            open(editor, frame, *request);
        if (!m_error.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::warning);
            ImGui::TextWrapped("%s", m_error.c_str());
            ImGui::PopStyleColor();
        }
        auto* document = m_material ? editor.materials().find(*m_material) : nullptr;
        if (!document)
        {
            if (m_error.empty())
                drawCenteredHint("Double-click a material file in Content to edit it here");
            return;
        }

        const auto& database = frame.assets.database();
        const auto* entry = database.find(assets::AssetId{ document->asset() });
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(assetIconColor(AssetKind::Material)), "%s", theme::icons::material);
        ImGui::SameLine();
        ImGui::Text("%.*s%s", static_cast<int>(document->fileName().size()), document->fileName().data(), document->dirty() ? "*" : "");
        ImGui::SetItemTooltip("%s%s", document->path().c_str(), document->dirty() ? "\nUnsaved changes: File > Save writes them" : "");
        if (!entry)
            ImGui::TextDisabled("The file is no longer in the project: its edits cannot be saved");
        ImGui::Separator();

        if (AssetId{ document->file().templateId } != assets::engine::pbrTemplate)
        {
            ImGui::TextDisabled("%s is not a material template", std::format("{}", AssetId{ document->file().templateId }).c_str());
            return;
        }
        const bool readOnly = !entry || entry->root >= database.roots().size() || database.roots()[entry->root].readOnly;
        if (entry && readOnly)
            ImGui::TextDisabled("%s Engine content is read-only", theme::icons::locked);
        drawParameters(editor, frame, *document, readOnly);
    }

private:
    using AssetId = assets::AssetId;

    // Shows the material file 'id', reading it unless it is open already (then it keeps its edits).
    void open(EditorContext& editor, FrameContext& frame, std::uint64_t id)
    {
        m_error.clear();
        if (editor.materials().find(id))
        {
            m_material = id;
            return;
        }
        const auto& database = frame.assets.database();
        const auto* entry = database.find(AssetId{ id });
        if (!entry || entry->type != assets::Material::assetType || !entry->key.empty())
        {
            m_error = std::format("{} is not a material file", AssetId{ id });
            return;
        }
        const auto text = readText(database.absolutePath(*entry));
        if (!text)
        {
            m_error = std::format("Cannot read '{}'", entry->path);
            return;
        }
        auto file = serialize::parseMaterial(*text);
        if (!file)
        {
            m_error = std::format("Cannot open '{}': {}", entry->path, serialize::describe(file.error()));
            return;
        }
        editor.materials().open(id, entry->path, std::move(*file));
        m_material = id;
    }

    // The parameters as a property grid with a reset button per row the file sets.
    void drawParameters(EditorContext& editor, FrameContext& frame, MaterialDocument& document, bool readOnly)
    {
        if (!m_type)
        {
            m_parameters = templateParameters(*frame.assets.defaults().pbrTemplate);
            m_type = materialType(m_parameters);
            m_rows = propertyRows(*m_type);
        }

        float widest = 0.0f;
        for (const auto& row : m_rows)
            widest = std::max(widest, ImGui::CalcTextSize(row.label.c_str()).x);
        const float width = ImGui::GetContentRegionAvail().x;
        const float labelWidth = std::clamp(widest + 2.0f * ImGui::GetStyle().CellPadding.x, labelMinShare * width, labelMaxShare * width);
        if (!ImGui::BeginTable("##parameters", 3, ImGuiTableFlags_SizingStretchProp))
            return;
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Reset", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());

        for (std::size_t i = 0; i < m_rows.size(); ++i)
        {
            const auto& row = m_rows[i];
            const auto& parameter = m_parameters[i];
            const bool set = document.parameter(parameter.name) != nullptr;
            ImGui::PushID(parameter.name.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            if (set)
                ImGui::PushStyleColor(ImGuiCol_Text, theme::accent);
            ImGui::TextUnformatted(row.label.c_str());
            if (set)
                ImGui::PopStyleColor();
            ImGui::SetItemTooltip("%s: %s", parameter.name.c_str(), set ? "set in the file" : "the template's default");

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::BeginDisabled(readOnly);
            const auto edited = drawPropertyValue(*m_type, row, row.field->getter(&document), frame.assets.database(), !readOnly);
            const bool active = ImGui::IsItemActive();
            const bool deactivated = ImGui::IsItemDeactivated();
            ImGui::EndDisabled();

            ImGui::TableNextColumn();
            const bool reset = set && !readOnly && theme::iconButton(theme::icons::resetValue, "##reset");
            if (set && !readOnly)
                ImGui::SetItemTooltip("Reset to the template's default");

            if (edited)
            {
                if (auto node = parameterNode(parameter, *edited); !node)
                    log::warn("Cannot set {}: {}", parameter.name, describe(node.error()));
                else
                    edit(editor, frame, document, parameter.name, std::move(*node), active);
            }
            if (reset)
                edit(editor, frame, document, parameter.name, std::nullopt, false);
            if (deactivated)
                editor.undoStack().seal();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    static void edit(EditorContext& editor, FrameContext& frame, const MaterialDocument& document, const std::string& parameter, std::optional<reflect::Node> value, bool continuous)
    {
        if (auto edited = editMaterial(editWorld(editor, frame), editor.undoStack(), document.asset(), parameter, std::move(value), continuous); !edited)
            log::warn("Cannot set {} of '{}': {}", parameter, document.fileName(), describe(edited.error()));
    }

    std::optional<std::uint64_t> m_material; // The material file shown
    std::string m_error;                     // Why the last one asked for could not be opened
    std::vector<MaterialParameterInfo> m_parameters;
    std::optional<reflect::TypeInfo> m_type; // Of m_parameters, which m_rows point into
    std::vector<PropertyRow> m_rows;
};
}

auto makeMaterialPanel() -> std::unique_ptr<Panel>
{
    return std::make_unique<MaterialPanel>();
}
}
