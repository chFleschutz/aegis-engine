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
import aegis.scripting.script;
import aegis.serialize;
import aegis.world;
import :panels;
import :theme;

namespace aegis::editor
{
namespace
{
// The Euler angles (degrees) of the rotation being dragged. Converting the quaternion back every frame would make
// the drag jump at the gimbal boundaries, so they are kept while the widget is active.
struct RotationDrag
{
    ImGuiID id{ 0 };
    glm::vec3 euler{ 0.0f };
};

// The value held as T, or T's default for a value of another kind (a getter always returns its field's kind).
template<class T>
auto as(const reflect::Value& value) -> T
{
    const T* held = std::get_if<T>(&value);
    return held ? *held : T{};
}

template<class T>
constexpr auto dataType() -> ImGuiDataType
{
    if constexpr (std::same_as<T, float>)
        return ImGuiDataType_Float;
    else if constexpr (std::same_as<T, double>)
        return ImGuiDataType_Double;
    else if constexpr (std::same_as<T, std::int64_t>)
        return ImGuiDataType_S64;
    else
        return ImGuiDataType_U64;
}

// The printf format of a value of T with 'decimals' digits after a floating-point value's point and 'unit' after it:
// spaced, but degrees right after the number. Empty for an integer without a unit (ImGui's default).
template<class T>
auto valueFormat(std::string_view unit, int decimals) -> std::string
{
    std::string suffix{ unit.empty() || unit == "°" ? "" : " " };
    for (const char c : unit)
        suffix += c == '%' ? std::string_view{ "%%" } : std::string_view{ &c, 1 }; // printf's percent sign is doubled
    if constexpr (std::floating_point<T>)
        return std::format("%.{}f{}", decimals, suffix);
    else if (unit.empty())
        return {};
    else
        return std::format("{}{}", std::signed_integral<T> ? "%lld" : "%llu", suffix);
}

// Drags 'count' values of T, or slides them for a Slider row, within the row's limits; a floating-point value shows
// 'decimals' digits after the point (and a drag rounds it to them).
template<class T>
auto dragValues(const PropertyRow& row, T* values, int count, int decimals = 3) -> bool
{
    const std::optional<T> min = row.min ? std::optional<T>{ static_cast<T>(*row.min) } : std::nullopt;
    const std::optional<T> max = row.max ? std::optional<T>{ static_cast<T>(*row.max) } : std::nullopt;
    const T* minPtr = min ? &*min : nullptr;
    const T* maxPtr = max ? &*max : nullptr;
    const std::string format = valueFormat<T>(row.unit, decimals);
    if (row.widget == PropertyWidget::Slider && min && max)
        return ImGui::SliderScalarN("##value", dataType<T>(), values, count, minPtr, maxPtr, format.empty() ? nullptr : format.c_str(), ImGuiSliderFlags_AlwaysClamp);
    return ImGui::DragScalarN("##value", dataType<T>(), values, count, static_cast<float>(row.step), minPtr, maxPtr, format.empty() ? nullptr : format.c_str(), ImGuiSliderFlags_AlwaysClamp);
}

// The axis strip's width at 100% scale, and the decimals a vector's components show (one-value fields show three:
// a camera's near plane can be 0.001).
constexpr float axisStripWidth{ 4.0f };
constexpr int axisDecimals{ 2 };
// Degrees of a rotation's angles below which they are shown as zero: the quaternion's conversion leaves that much.
constexpr float rotationNoise{ 1e-4f };

// The letter of a vector's component, in the strip's tooltip.
auto axisLetter(int component) -> const char*
{
    constexpr std::array letters{ "X", "Y", "Z", "W" };
    return letters[static_cast<std::size_t>(component)];
}

// The color of that strip: the axis colors, and a neutral for W.
auto axisColor(int component) -> ImVec4
{
    switch (component)
    {
    case 0: return theme::axisX;
    case 1: return theme::axisY;
    case 2: return theme::axisZ;
    default: return theme::textDisabled;
    }
}

// Drags the 'count' components of a vector, sharing the width equally, each field marked by a thin strip in its axis
// color along its left edge. A click on the strip resets that component to its value in 'defaults', which is called
// only while a strip is hovered or clicked. The components form one group, so the caller sees the whole vector as one
// item: a drag stays one undo entry.
template<class Defaults>
auto dragAxes(const PropertyRow& row, float* values, int count, Defaults&& defaults) -> bool
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const float spacing = style.ItemSpacing.x;
    const float stripWidth = std::round(axisStripWidth * theme::scale());
    const float height = ImGui::GetFrameHeight();
    const float componentWidth = (ImGui::GetContentRegionAvail().x - spacing * static_cast<float>(count - 1)) / static_cast<float>(count);

    bool changed = false;
    ImGui::BeginGroup();
    for (int i = 0; i < count; ++i)
    {
        ImGui::PushID(i);
        if (i > 0)
            ImGui::SameLine(0.0f, spacing);

        // The strip's button first: of two overlapping items the first takes the hover, so the field gets the rest
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const bool reset = ImGui::InvisibleButton("##reset", ImVec2{ stripWidth, height });
        const bool stripHovered = ImGui::IsItemHovered();
        if (reset)
        {
            values[i] = defaults()[i];
            changed = true;
        }
        else if (ImGui::BeginItemTooltip())
        {
            ImGui::Text("Reset %s to %g", axisLetter(i), static_cast<double>(defaults()[i]));
            ImGui::EndTooltip();
        }

        ImGui::SetCursorScreenPos(start);
        ImGui::SetNextItemWidth(componentWidth);
        changed |= dragValues(row, &values[i], 1, axisDecimals);

        // Over the field's left edge, its corners rounded as the field's are
        const ImVec4 color = axisColor(i);
        const ImVec4 shown = stripHovered ? ImVec4{ std::min(color.x * 1.2f, 1.0f), std::min(color.y * 1.2f, 1.0f), std::min(color.z * 1.2f, 1.0f), color.w } : color;
        ImGui::GetWindowDrawList()->AddRectFilled(start, ImVec2{ start.x + stripWidth, start.y + height }, theme::packed(shown), style.FrameRounding, ImDrawFlags_RoundCornersLeft);
        ImGui::PopID();
    }
    ImGui::EndGroup();
    return changed;
}

// The vector field 'row' in a default-constructed instance of its type, padded with zeros to four components.
auto fieldDefaults(const reflect::TypeInfo& type, const PropertyRow& row) -> glm::vec4
{
    glm::vec4 defaults{ 0.0f };
    const auto instance = type.create();
    if (!instance)
        return defaults;
    const auto value = row.field->getter(instance->get());
    if (const auto* v2 = std::get_if<glm::vec2>(&value))
        defaults = glm::vec4{ *v2, 0.0f, 0.0f };
    else if (const auto* v3 = std::get_if<glm::vec3>(&value))
        defaults = glm::vec4{ *v3, 0.0f };
    else if (const auto* v4 = std::get_if<glm::vec4>(&value))
        defaults = *v4;
    return defaults;
}

template<class T>
auto dragValue(const reflect::TypeInfo& type, const PropertyRow& row, const reflect::Value& value) -> std::optional<reflect::Value>
{
    T edited = as<T>(value);
    if constexpr (std::same_as<T, glm::vec2> || std::same_as<T, glm::vec3> || std::same_as<T, glm::vec4>)
    {
        if (dragAxes(row, &edited.x, row.components, [&] { return fieldDefaults(type, row); }))
            return reflect::Value{ edited };
    }
    else if (dragValues(row, &edited, 1))
    {
        return reflect::Value{ edited };
    }
    return std::nullopt;
}

auto drawNumber(const reflect::TypeInfo& type, const PropertyRow& row, const reflect::Value& value) -> std::optional<reflect::Value>
{
    switch (row.field->kind)
    {
    case reflect::FieldKind::Int: return dragValue<std::int64_t>(type, row, value);
    case reflect::FieldKind::UInt: return dragValue<std::uint64_t>(type, row, value);
    case reflect::FieldKind::Float: return dragValue<float>(type, row, value);
    case reflect::FieldKind::Double: return dragValue<double>(type, row, value);
    case reflect::FieldKind::Vec2: return dragValue<glm::vec2>(type, row, value);
    case reflect::FieldKind::Vec3: return dragValue<glm::vec3>(type, row, value);
    case reflect::FieldKind::Vec4: return dragValue<glm::vec4>(type, row, value);
    default: return std::nullopt;
    }
}

// Radians in the field, degrees in the widget (the row's limits are in degrees already).
auto drawAngle(const PropertyRow& row, const reflect::Value& value) -> std::optional<reflect::Value>
{
    const bool isDouble = row.field->kind == reflect::FieldKind::Double;
    const double radians = isDouble ? as<double>(value) : static_cast<double>(as<float>(value));
    float degrees = static_cast<float>(glm::degrees(radians));
    if (!dragValues(row, &degrees, 1))
        return std::nullopt;
    const double edited = glm::radians(static_cast<double>(degrees));
    return isDouble ? reflect::Value{ edited } : reflect::Value{ static_cast<float>(edited) };
}

// A linear color, shown and picked in sRGB: the swatch and hex, with floats in the picker, converted back on an edit.
// A channel the pick leaves alone keeps its stored value, so an unchanged pick writes nothing.
auto drawColor(const PropertyRow& row, const reflect::Value& value) -> std::optional<reflect::Value>
{
    const bool isVec4 = row.field->kind == reflect::FieldKind::Vec4;
    const glm::vec4 stored = isVec4 ? as<glm::vec4>(value) : glm::vec4{ as<glm::vec3>(value), 1.0f };
    // A color above 1, or a field allowed above 1, is HDR: no clamping, and shown as floats since hex cannot
    const bool hdr = (row.max && *row.max > 1.0) || std::max({ stored.r, stored.g, stored.b }) > 1.0f;
    const ImGuiColorEditFlags flags = ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoTooltip | (hdr ? ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_DisplayRGB : ImGuiColorEditFlags_DisplayHex);

    const glm::vec3 shown = linearToSrgb(glm::vec3{ stored });
    glm::vec4 edited{ shown, stored.a };
    const bool changed = isVec4 ? ImGui::ColorEdit4("##value", &edited.x, flags) : ImGui::ColorEdit3("##value", &edited.x, flags);
    ImGui::SetItemTooltip("Stored linear");
    if (!changed)
        return std::nullopt;

    const glm::vec3 linear = linearAfterEdit(glm::vec3{ stored }, shown, glm::vec3{ edited });
    return isVec4 ? reflect::Value{ glm::vec4{ linear, edited.a } } : reflect::Value{ linear };
}

auto drawRotation(const PropertyRow& row, const reflect::Value& value) -> std::optional<reflect::Value>
{
    static RotationDrag s_drag;
    const ImGuiID id = ImGui::GetID("##value");
    glm::vec3 euler = s_drag.id == id ? s_drag.euler : glm::degrees(glm::eulerAngles(as<glm::quat>(value)));
    // The conversion leaves noise around zero, which would show as -0.00: a zero is a zero
    for (int i = 0; i < 3; ++i)
        euler[i] = std::abs(euler[i]) < rotationNoise ? 0.0f : euler[i];
    const bool changed = dragAxes(row, &euler.x, 3, [] { return glm::vec4{ 0.0f }; });
    if (ImGui::IsItemActive())
        s_drag = { .id = id, .euler = euler };
    else if (s_drag.id == id)
        s_drag = {};
    return changed ? std::optional{ reflect::Value{ glm::quat{ glm::radians(euler) } } } : std::nullopt;
}

auto drawCombo(const PropertyRow& row, const reflect::Value& value) -> std::optional<reflect::Value>
{
    const auto current = as<std::int64_t>(value);
    const auto& entries = row.field->enumEntries;
    const auto selected = std::ranges::find(entries, current, &reflect::EnumEntry::value);
    std::optional<reflect::Value> edited;
    if (ImGui::BeginCombo("##value", selected != entries.end() ? selected->name.c_str() : "?"))
    {
        for (const auto& entry : entries)
        {
            if (ImGui::Selectable(entry.name.c_str(), entry.value == current))
                edited = reflect::Value{ entry.value };
        }
        ImGui::EndCombo();
    }
    return edited;
}

auto drawText(const reflect::Value& value) -> std::optional<reflect::Value>
{
    auto text = as<std::string>(value);
    return ImGui::InputText("##value", &text) ? std::optional{ reflect::Value{ std::move(text) } } : std::nullopt;
}

// An asset as an asset slot and its picker show it.
struct SlotAsset
{
    std::uint64_t id{ 0 };
    const char* icon{ theme::icons::none };
    std::string name;     // Its short name (assetDisplayName)
    std::string location; // Its file's path, and the key of a part
    std::string file;     // Its file's name
    std::string type;
    bool readOnly{ false }; // Engine content
    bool missing{ false };  // Not in the database
};

auto slotAsset(const assets::AssetDatabase& database, std::uint64_t id) -> SlotAsset
{
    if (id == 0)
        return SlotAsset{ .name = "None" };
    const auto* entry = database.find(assets::AssetId{ id });
    if (!entry)
        return SlotAsset{ .id = id, .icon = theme::icons::warning, .name = "Missing asset", .location = std::format("{} is not in the project", assets::AssetId{ id }), .missing = true };
    const auto& roots = database.roots();
    return SlotAsset{
        .id = id,
        .icon = assetIcon(assetKind(entry->type, entry->path)),
        .name = assetDisplayName(entry->path, entry->key, entry->type),
        .location = entry->key.empty() ? entry->path : std::format("{} : {}", entry->path, entry->key),
        .file = entry->path.substr(entry->path.rfind('/') + 1),
        .type = entry->type,
        .readOnly = entry->root < roots.size() && roots[entry->root].readOnly,
    };
}

// Draws 'asset' on the line from 'left' to 'right' whose text starts at 'top': its icon, its name and, for engine
// content, the lock, which stays in view and cuts a long name short. Returns where the name (or the lock) ends.
auto drawAssetLabel(ImDrawList* drawList, float left, float right, float top, const SlotAsset& asset, bool selected) -> float
{
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const ImU32 muted = theme::packed(theme::mutedText(selected));
    const ImU32 iconColor = asset.missing ? theme::packed(theme::warning) : asset.id == 0 ? muted
                                                                                          : theme::packed(theme::textDim);
    const ImU32 nameColor = asset.id == 0 || asset.readOnly ? muted : ImGui::GetColorU32(ImGuiCol_Text);
    drawList->AddText(ImVec2{ left, top }, iconColor, asset.icon);
    const float nameX = left + ImGui::CalcTextSize(asset.icon).x + spacing;
    const float lockWidth = asset.readOnly ? ImGui::CalcTextSize(theme::icons::locked).x + spacing : 0.0f;
    const float nameEnd = std::min(nameX + ImGui::CalcTextSize(asset.name.c_str()).x, right - lockWidth);
    const ImVec4 clip{ nameX, top, nameEnd, top + ImGui::GetFontSize() };
    drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{ nameX, top }, nameColor, asset.name.c_str(), nullptr, 0.0f, &clip);
    if (!asset.readOnly)
        return nameEnd;
    drawList->AddText(ImVec2{ nameEnd + spacing, top }, muted, theme::icons::locked);
    return nameEnd + lockWidth;
}

// The assets an asset slot's picker lists: those of its type whose name or location holds its search, by name.
struct PickerList
{
    std::string type;
    std::string filter;
    std::size_t databaseSize{ 0 };
    std::vector<SlotAsset> assets;
};

// The picker's rows in view before it scrolls; a row's padding above and below its label, the gap between rows and the
// rounding of a row's highlight, in pixels at 100% (the hierarchy's); the picker's least width, in font sizes.
constexpr int pickerRows{ 12 };
constexpr float pickerRowPadding{ 3.0f };
constexpr float pickerRowGap{ 1.0f };
constexpr float pickerRounding{ 2.0f };
constexpr float pickerMinWidth{ 22.0f };

// 'text' in lower case, for sorting by name.
auto lowered(std::string_view text) -> std::string
{
    std::string result{ text };
    std::ranges::transform(result, result.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return result;
}

// A row of the picker across its width: the asset, and its file's name right-aligned where there is room (its location in
// the tooltip). The highlight
// is drawn by hand, with rounded corners (ImGui's is square). True when it was picked.
auto pickerRow(const SlotAsset& asset, bool selected, float height) -> bool
{
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const ImVec4 clear{ 0.0f, 0.0f, 0.0f, 0.0f };
    ImGui::PushStyleColor(ImGuiCol_Header, clear);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, clear);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, clear);
    const bool picked = ImGui::Selectable("##asset", selected, ImGuiSelectableFlags_None, ImVec2{ 0.0f, height });
    ImGui::PopStyleColor(3);

    // The item also covers the gap to the next row and half the spacing at its left: the highlight covers the row
    const float left = ImGui::GetItemRectMin().x;
    const float right = ImGui::GetItemRectMax().x;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (selected || ImGui::IsItemHovered())
    {
        const ImGuiCol highlight = selected ? ImGuiCol_Header : ImGui::IsItemActive() ? ImGuiCol_HeaderActive
                                                                                      : ImGuiCol_HeaderHovered;
        drawList->AddRectFilled(ImVec2{ left, position.y }, ImVec2{ right, position.y + height }, ImGui::GetColorU32(highlight), std::round(pickerRounding * theme::scale()));
    }

    const float top = position.y + std::round((height - ImGui::GetFontSize()) * 0.5f);
    const float textRight = right - (position.x - left);
    const float nameEnd = drawAssetLabel(drawList, position.x, textRight, top, asset, selected);
    if (asset.location.empty())
        return picked;
    // The file tells assets of one name apart: after the name, if it fits
    const float width = ImGui::CalcTextSize(asset.file.c_str()).x;
    if (textRight - width >= nameEnd + 2.0f * ImGui::GetStyle().ItemSpacing.x)
        drawList->AddText(ImVec2{ textRight - width, top }, theme::packed(theme::mutedText(selected)), asset.file.c_str());
    ImGui::SetItemTooltip("%s", asset.location.c_str());
    return picked;
}

// The asset in the slot by its icon and short name, and a picker of the assets of the field's type (and None): a search
// that takes the keyboard and stays in place above the list, Enter picking the first match, and the list by name, opened
// at the asset in the slot. An 'editable' slot takes an asset of its type dragged from the asset browser.
auto drawAssetSlot(const PropertyRow& row, const reflect::Value& value, const assets::AssetDatabase& database, bool editable) -> std::optional<reflect::Value>
{
    static std::string s_filter;
    static PickerList s_list;
    const auto current = as<std::uint64_t>(value);
    const SlotAsset shown = slotAsset(database, current);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float fontSize = ImGui::GetFontSize();

    // The slot draws its own preview over an empty one: ImGui's is text of one color
    ImDrawList* slotDrawList = ImGui::GetWindowDrawList();
    const ImVec2 slotMin = ImGui::GetCursorScreenPos();
    const float slotWidth = ImGui::CalcItemWidth();
    ImGui::SetNextWindowSizeConstraints(ImVec2{ std::max(slotWidth, pickerMinWidth * fontSize), 0.0f }, ImVec2{ FLT_MAX, FLT_MAX });
    const bool open = ImGui::BeginCombo("##value", "", ImGuiComboFlags_NoArrowButton);
    const float slotRight = slotMin.x + slotWidth - style.FramePadding.x;
    slotDrawList->PushClipRect(slotMin, ImVec2{ slotMin.x + slotWidth, slotMin.y + ImGui::GetFrameHeight() }, true);
    drawAssetLabel(slotDrawList, slotMin.x + style.FramePadding.x, slotRight, slotMin.y + style.FramePadding.y, shown, false);
    slotDrawList->PopClipRect();

    std::optional<reflect::Value> edited;
    if (!open && editable && ImGui::BeginDragDropTarget())
    {
        const auto& slotType = row.field->assetType;
        if (const auto dropped = acceptAssetPayload([&slotType](const AssetPayload& payload) { return acceptsAsset(slotType, payload); }))
            edited = reflect::Value{ dropped->id };
        ImGui::EndDragDropTarget();
    }
    if (open)
    {
        const bool appearing = ImGui::IsWindowAppearing();
        if (appearing)
        {
            s_filter.clear();
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool entered = searchField("##filter", "Search", s_filter, ImGuiInputTextFlags_EnterReturnsTrue);

        // The matching assets, listed again when the picker opens or the search, the slot's type or the database changed
        if (appearing || s_list.filter != s_filter || s_list.type != row.field->assetType || s_list.databaseSize != database.size())
        {
            s_list = PickerList{ .type = row.field->assetType, .filter = s_filter, .databaseSize = database.size() };
            for (const auto& entry : database.entries())
            {
                if (entry.type != row.field->assetType)
                    continue;
                auto asset = slotAsset(database, entry.id.value);
                if (core::containsIgnoringCase(asset.name, s_filter) || core::containsIgnoringCase(asset.location, s_filter))
                    s_list.assets.push_back(std::move(asset));
            }
            std::ranges::sort(s_list.assets, {}, [](const SlotAsset& asset) { return std::pair{ lowered(asset.name), lowered(asset.location) }; });
        }
        if (entered && !s_filter.empty() && !s_list.assets.empty())
        {
            edited = reflect::Value{ s_list.assets.front().id };
            ImGui::CloseCurrentPopup();
        }

        // None comes first while there is no search
        const float scale = theme::scale();
        const float rowHeight = std::round(fontSize + 2.0f * pickerRowPadding * scale);
        const float rowGap = std::round(pickerRowGap * scale);
        const int noneRows = s_filter.empty() ? 1 : 0;
        const int count = static_cast<int>(s_list.assets.size()) + noneRows;
        const float listHeight = static_cast<float>(std::clamp(count, 1, pickerRows)) * (rowHeight + rowGap) - rowGap;
        if (ImGui::BeginChild("##assets", ImVec2{ 0.0f, listHeight }, ImGuiChildFlags_NavFlattened))
        {
            if (count == 0)
                ImGui::TextDisabled("No %s matches the search", row.field->assetType.c_str());

            static const SlotAsset none{ .name = "None" };
            const auto found = std::ranges::find(s_list.assets, current, &SlotAsset::id);
            const int currentIndex = current == 0 ? 0 : found != s_list.assets.end() ? static_cast<int>(found - s_list.assets.begin()) + noneRows
                                                                                     : -1;
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ style.ItemSpacing.x, rowGap });
            ImGuiListClipper clipper; // Only the rows in view are drawn
            clipper.Begin(count, rowHeight + rowGap);
            if (appearing && currentIndex >= 0)
                clipper.IncludeItemByIndex(currentIndex);
            while (clipper.Step())
            {
                for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
                {
                    const SlotAsset& asset = index < noneRows ? none : s_list.assets[static_cast<std::size_t>(index - noneRows)];
                    ImGui::PushID(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(asset.id)));
                    if (pickerRow(asset, asset.id == current, rowHeight))
                        edited = reflect::Value{ asset.id };
                    if (appearing && index == currentIndex)
                        ImGui::SetScrollHereY(0.5f);
                    ImGui::PopID();
                }
            }
            ImGui::PopStyleVar();
        }
        ImGui::EndChild();
        ImGui::EndCombo();
    }
    if (shown.location.empty())
        ImGui::SetItemTooltip("No %s", row.field->assetType.c_str());
    else
        ImGui::SetItemTooltip("%s\n%s", shown.location.c_str(), shown.missing ? row.field->assetType.c_str() : shown.type.c_str());
    return edited;
}

// The row's widget for 'value'; the new value when it was edited this frame.
auto drawValue(const reflect::TypeInfo& type, const PropertyRow& row, const reflect::Value& value, const assets::AssetDatabase& database, bool editable) -> std::optional<reflect::Value>
{
    switch (row.widget)
    {
    case PropertyWidget::Checkbox:
    {
        bool checked = as<bool>(value);
        return ImGui::Checkbox("##value", &checked) ? std::optional{ reflect::Value{ checked } } : std::nullopt;
    }
    case PropertyWidget::Drag:
    case PropertyWidget::Slider: return drawNumber(type, row, value);
    case PropertyWidget::Angle: return drawAngle(row, value);
    case PropertyWidget::Color: return drawColor(row, value);
    case PropertyWidget::Rotation: return drawRotation(row, value);
    case PropertyWidget::Combo: return drawCombo(row, value);
    case PropertyWidget::Text: return drawText(value);
    case PropertyWidget::AssetSlot: return drawAssetSlot(row, value, database, editable);
    }
    return std::nullopt;
}

// The label column's share of a section's width: at least, and at most for a long label.
constexpr float labelMinShare{ 0.3f };
constexpr float labelMaxShare{ 0.4f };

// The registered fields of 'object' (the entity's component or script of 'type') as a two-column property grid.
// Each edit is recorded in the undo stack; a widget still being dragged or typed in extends one entry.
void drawFields(const reflect::TypeInfo& type, void* object, world::Entity entity, std::optional<std::size_t> scriptIndex, bool readOnly, EditorContext& editor, FrameContext& frame)
{
    const auto rows = propertyRows(type);
    // The values take most of the width (three axes share a row): the labels get what their widest needs, at least
    // labelMinShare of the width so the sections line up, and at most labelMaxShare
    float widest = 0.0f;
    for (const auto& row : rows)
        widest = std::max(widest, ImGui::CalcTextSize(row.label.c_str()).x);
    const float width = ImGui::GetContentRegionAvail().x;
    const float labelWidth = std::clamp(widest + 2.0f * ImGui::GetStyle().CellPadding.x, labelMinShare * width, labelMaxShare * width);
    if (rows.empty() || !ImGui::BeginTable("##fields", 2, ImGuiTableFlags_SizingStretchProp))
        return;
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

    const auto resolve = [&frame](const reflect::TypeInfo& resolved, void* component) { frame.assets.resolve(resolved, component, frame.types); };
    for (const auto& row : rows)
    {
        ImGui::PushID(row.field->name.c_str());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(row.label.c_str());
        if (!row.field->meta.tooltip.empty())
            ImGui::SetItemTooltip("%s", row.field->meta.tooltip.c_str());

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::BeginDisabled(readOnly || row.readOnly);
        const auto current = row.field->getter(object);
        const auto edited = drawValue(type, row, current, frame.assets.database(), !readOnly && !row.readOnly);
        const bool active = ImGui::IsItemActive();
        const bool deactivated = ImGui::IsItemDeactivated();
        ImGui::EndDisabled();
        if (edited)
        {
            const auto prepare = row.widget == PropertyWidget::AssetSlot ? PrepareField{ resolve } : PrepareField{};
            if (auto written = setField(type, *row.field, editor.world()->registry(), entity, object, *edited, prepare); !written)
            {
                log::warn("Cannot set {}.{}: {}", type.name, row.field->name, reflect::describe(written.error()));
            }
            else
            {
                const auto id = editor.world()->registry().get<StableId>(entity);
                editor.undoStack().record(FieldEdit{ .entity = id, .type = type.name, .field = row.field->name, .scriptIndex = scriptIndex, .oldValue = current, .newValue = *edited }, active);
            }
        }
        if (deactivated)
            editor.undoStack().seal();
        ImGui::PopID();
    }
    ImGui::EndTable();
}

// The Add menu's items: 'types' (sorted by category) as selectable rows, under a heading per category when
// 'categories'. 'highlighted' (the first match) is drawn selected. The type clicked this frame, if any.
auto drawTypeGroups(std::span<const reflect::TypeInfo* const> types, const reflect::TypeInfo* highlighted, bool categories) -> const reflect::TypeInfo*
{
    const reflect::TypeInfo* picked = nullptr;
    const std::string* category = nullptr;
    for (const auto* type : types)
    {
        if (categories && (!category || *category != type->meta.category))
        {
            category = &type->meta.category;
            ImGui::SeparatorText(category->empty() ? "General" : category->c_str());
        }
        const std::string label = std::format("{} {}", componentIcon(*type), type->displayName());
        ImGui::PushID(type->name.c_str());
        if (ImGui::Selectable(label.c_str(), type == highlighted))
            picked = type;
        ImGui::PopID();
    }
    return picked;
}

// A square "more" button (three dots) of 'side' at the cursor: true when clicked. 'id' is unique among the window's items.
auto menuButton(const char* id, float side) -> bool
{
    const bool pressed = ImGui::InvisibleButton(id, ImVec2{ side, side });
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    auto* drawList = ImGui::GetWindowDrawList();
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered), ImGui::GetStyle().FrameRounding);
    const float radius = std::max(1.0f, std::round(side * 0.075f));
    const float spacing = radius * 3.0f;
    const ImVec2 center{ (min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f };
    for (int i = -1; i <= 1; ++i)
        drawList->AddCircleFilled(ImVec2{ center.x, center.y + static_cast<float>(i) * spacing }, radius, ImGui::GetColorU32(ImGuiCol_Text));
    return pressed;
}

// What a section's menu asks for.
enum class SectionAction
{
    None,
    Remove, ///< Remove the component, detach the script
};

// The menu of a section: Reset, Copy, Paste and Remove (Detach for a script), applied to the entity's component or
// script 'type' at 'object'. Reset and Paste are one undo entry each.
auto drawSectionMenu(const reflect::TypeInfo& type, void* object, world::Entity entity, std::optional<std::size_t> scriptIndex, bool readOnly, EditorContext& editor, FrameContext& frame) -> SectionAction
{
    auto action = SectionAction::None;
    if (!ImGui::BeginPopup("##section"))
        return action;

    const bool hasFields = !type.fields.empty();
    if (ImGui::MenuItem("Reset", nullptr, false, !readOnly && hasFields))
    {
        if (auto reset = resetFields(editWorld(editor, frame), editor.undoStack(), entity, type, scriptIndex); !reset)
            log::warn("Cannot reset {}: {}", type.name, reset.error());
    }
    if (ImGui::MenuItem("Copy", nullptr, false, hasFields))
    {
        if (auto text = componentToText(type, object))
            ImGui::SetClipboardText(text->c_str());
        else
            log::warn("{}", text.error());
    }
    const char* clipboard = ImGui::GetClipboardText();
    const bool pastable = !readOnly && hasFields && clipboard && componentFromText(type, clipboard).has_value();
    if (ImGui::MenuItem("Paste", nullptr, false, pastable))
    {
        if (auto pasted = pasteFields(editWorld(editor, frame), editor.undoStack(), entity, type, scriptIndex, clipboard); !pasted)
            log::warn("Cannot paste {}: {}", type.name, pasted.error());
    }
    ImGui::Separator();
    const bool removable = !readOnly && !type.meta.required;
    if (ImGui::MenuItem(scriptIndex ? "Detach" : "Remove", nullptr, false, removable))
        action = SectionAction::Remove;
    ImGui::EndPopup();
    return action;
}

// The width of the open chevron's ink, as a share of the font size (Lucide's chevron-down spans 0.58 of its em).
constexpr float chevronShare{ 0.6f };

// A header per type: open with its fields, a leaf for a tag (no fields), a right click opening its menu. Returns false when the menu asked to remove the component or detach the script.
auto drawSection(const reflect::TypeInfo& type, void* object, world::Entity entity, std::optional<std::size_t> scriptIndex, bool readOnly, EditorContext& editor, FrameContext& frame) -> bool
{
    // ImGui's header in the controls' grey, its own label and triangle invisible: the chevron, the icon and the name
    // are drawn over it. Identified by the name (the ID stack holds the type), so its open state stays.
    const std::string id = std::format("###{}", type.displayName());
    const bool leaf = type.fields.empty();
    const ImGuiTreeNodeFlags flags = (leaf ? ImGuiTreeNodeFlags_Leaf : ImGuiTreeNodeFlags_DefaultOpen);
    const float left = ImGui::GetCursorScreenPos().x;
    ImGui::PushStyleColor(ImGuiCol_Header, theme::bgFrame);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, theme::bgFrameHover);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, theme::bgFrameActive);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });
    const bool open = ImGui::CollapsingHeader(id.c_str(), flags);
    ImGui::PopStyleColor(4);
    const bool menuRequested = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    const ImVec2 min = ImGui::GetItemRectMin();

    // Each in a slot of its own, the same gap apart: the chevron's as wide as its open ink (the closed one is narrower,
    // centered in it), the icon's its advance (the icons' ink is centered in it), so the names line up across the
    // sections and don't move as one opens. A tag's section has no chevron; its slot stays empty.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float fontSize = ImGui::GetFontSize();
    const float gap = style.ItemInnerSpacing.x;
    const float chevronSlot = std::round(chevronShare * fontSize);
    const float textY = min.y + style.FramePadding.y;
    const float chevronX = left + style.FramePadding.x;
    const float iconX = chevronX + chevronSlot + gap;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 textColor = theme::packed(theme::text);
    if (!leaf)
        drawList->AddText(ImVec2{ std::round(chevronX + (chevronSlot - fontSize) * 0.5f), textY }, textColor, open ? theme::icons::expanded : theme::icons::collapsed);
    drawList->AddText(ImVec2{ iconX, textY }, textColor, componentIcon(type));
    const std::string name{ type.displayName() };
    drawList->AddText(ImVec2{ iconX + fontSize + gap, textY }, textColor, name.c_str());

    if (menuRequested)
        ImGui::OpenPopup("##section");
    const auto action = drawSectionMenu(type, object, entity, scriptIndex, readOnly, editor, frame);

    if (open)
        drawFields(type, object, entity, scriptIndex, readOnly, editor, frame);
    ImGui::Spacing();
    return action != SectionAction::Remove;
}

// The active selected entity, with the size of a larger selection: its name (the ID in a tooltip), its registered components and its scripts, each generated from the type
// registry, and the Add menu. Edits apply directly: the panel runs at the frame's safe point.
class InspectorPanel final : public Panel
{
public:
    InspectorPanel() { setOpen(true); }

    [[nodiscard]] auto name() const -> std::string_view override { return "Inspector"; }
    [[nodiscard]] auto icon() const -> const char* override { return theme::icons::inspector; }
    [[nodiscard]] auto column() const -> bool override { return true; }

    void draw(EditorContext& editor, FrameContext& frame) override
    {
        const auto entity = editor.selectedEntity();
        if (!entity)
        {
            drawCenteredHint("Select an entity in the Hierarchy or the viewport");
            return;
        }

        auto& registry = editor.world()->registry();
        ImGui::PushID(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(registry.get<StableId>(entity).value)));
        // A scene instance's entities are not saved (the holder's SceneInstance recreates them): an edit would be lost
        const bool readOnly = registry.has<serialize::InstanceMember>(entity);
        drawHeader(registry, entity, readOnly, editor, frame);
        // It shows the active entity of a larger selection
        if (const auto count = editor.selection().ids().size(); count > 1)
            ImGui::TextDisabled("%zu selected", count);
        drawComponents(registry, entity, readOnly, editor, frame);
        drawScripts(registry, entity, readOnly, editor, frame);
        if (!readOnly)
            drawAddMenu(registry, entity, editor, frame);
        ImGui::PopID();
    }

private:
    static void drawHeader(world::Registry& registry, world::Entity entity, bool readOnly, EditorContext& editor, FrameContext& frame)
    {
        const bool rename = editor.takeRenameRequest();
        const auto id = std::format("{:016x}", registry.get<StableId>(entity).value);
        const float side = ImGui::GetFrameHeight();
        // The entity's icon, as in its hierarchy row, then the name filling the row up to the menu button
        const auto kind = entityKind(registry, entity);
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, readOnly ? theme::packed(theme::textMuted) : entityIconColor(kind));
        ImGui::TextUnformatted(entityIcon(kind));
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-(side + ImGui::GetStyle().ItemSpacing.x));
        if (registry.has<Name>(entity))
        {
            ImGui::BeginDisabled(readOnly);
            if (rename && !readOnly)
                ImGui::SetKeyboardFocusHere(); // F2: the name field gets the keyboard, with its text selected
            auto name = registry.get<Name>(entity).name;
            if (ImGui::InputText("##name", &name, rename ? ImGuiInputTextFlags_AutoSelectAll : ImGuiInputTextFlags_None))
            {
                // Typing the name is one entry
                if (auto renamed = renameEntity(editWorld(editor, frame), editor.undoStack(), entity, std::move(name), ImGui::IsItemActive()); !renamed)
                    log::warn("Cannot rename the entity: {}", renamed.error());
            }
            if (ImGui::IsItemDeactivated())
                editor.undoStack().seal();
            ImGui::EndDisabled();
        }
        else
        {
            ImGui::BeginDisabled();
            std::string unnamed = "Unnamed";
            ImGui::InputText("##name", &unnamed, ImGuiInputTextFlags_ReadOnly);
            ImGui::EndDisabled();
        }
        // The ID is rarely needed: it shows when the name is hovered, also while the field is disabled
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled) && ImGui::BeginTooltip())
        {
            {
                const theme::MonospaceScope mono; // Popped before EndTooltip: the font stack is per window
                ImGui::Text("ID %s", id.c_str());
            }
            ImGui::EndTooltip();
        }

        ImGui::SameLine();
        if (menuButton("##entityMenu", side))
            ImGui::OpenPopup("##entity");
        if (ImGui::BeginPopup("##entity"))
        {
            if (ImGui::MenuItem("Copy ID"))
                ImGui::SetClipboardText(id.c_str());
            ImGui::EndPopup();
        }

        if (readOnly)
        {
            const auto holder = registry.find(StableId{ registry.get<serialize::InstanceMember>(entity).holder });
            const char* holderName = holder && registry.has<Name>(holder) ? registry.get<Name>(holder).name.c_str() : "?";
            ImGui::TextWrapped("Part of the scene instance '%s': read-only until overrides exist.", holderName);
        }
        ImGui::Separator();
    }

    static void drawComponents(world::Registry& registry, world::Entity entity, bool readOnly, EditorContext& editor, FrameContext& frame)
    {
        const reflect::TypeInfo* removed = nullptr;
        for (const auto& section : componentSections(frame.types, registry, entity))
        {
            ImGui::PushID(section.type->name.c_str());
            if (!drawSection(*section.type, section.object, entity, std::nullopt, readOnly, editor, frame))
                removed = section.type;
            ImGui::PopID();
        }
        if (removed)
        {
            if (auto done = removeComponent(editWorld(editor, frame), editor.undoStack(), entity, *removed); !done)
                log::warn("Cannot remove {}: {}", removed->name, done.error());
        }
    }

    static void drawScripts(world::Registry& registry, world::Entity entity, bool readOnly, EditorContext& editor, FrameContext& frame)
    {
        if (!registry.has<scripting::Scripts>(entity))
            return;
        auto& entries = registry.get<scripting::Scripts>(entity).entries;
        std::optional<std::size_t> detached;
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            const auto* type = frame.types.find(entries[i].name);
            if (!type || !entries[i].object)
                continue;
            ImGui::PushID(static_cast<int>(i));
            if (!drawSection(*type, entries[i].object, entity, i, readOnly, editor, frame))
                detached = i;
            ImGui::PopID();
        }
        if (detached)
        {
            if (auto done = detachScript(editWorld(editor, frame), editor.undoStack(), entity, *detached); !done)
                log::warn("Cannot detach the script: {}", done.error());
        }
    }

    // One full-width button opening a popup with a search field and every component and script the entity can get.
    static void drawAddMenu(world::Registry& registry, world::Entity entity, EditorContext& editor, FrameContext& frame)
    {
        static std::string s_filter;

        ImGui::Spacing();
        if (ImGui::Button(std::format("{} Add...", theme::icons::create).c_str(), ImVec2{ -FLT_MIN, 0.0f }))
            ImGui::OpenPopup("##add");

        ImGui::SetNextWindowSizeConstraints(ImVec2{ ImGui::GetItemRectSize().x, 0.0f }, ImVec2{ FLT_MAX, ImGui::GetTextLineHeightWithSpacing() * 20.0f });
        ImGui::SetNextWindowPos(ImVec2{ ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y }, ImGuiCond_Appearing); // Under the button, as wide as it
        if (!ImGui::BeginPopup("##add"))
            return;

        if (ImGui::IsWindowAppearing())
        {
            s_filter.clear();
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool confirmed = searchField("##filter", "Search", s_filter, ImGuiInputTextFlags_EnterReturnsTrue);

        const auto components = filterTypes(addableComponents(frame.types, registry, entity), s_filter);
        const auto scripts = filterTypes(scriptTypes(frame.types), s_filter);
        if (components.empty() && scripts.empty())
            ImGui::TextDisabled(s_filter.empty() ? "Nothing to add" : "No match");

        // The first match is what Enter adds
        const reflect::TypeInfo* first = !components.empty() ? components.front() : !scripts.empty() ? scripts.front()
                                                                                                     : nullptr;
        const reflect::TypeInfo* picked = confirmed ? first : nullptr;
        if (const auto* type = drawTypeGroups(components, first, true))
            picked = type;
        if (!scripts.empty())
        {
            ImGui::SeparatorText("Scripts");
            if (const auto* type = drawTypeGroups(scripts, first, false))
                picked = type;
        }

        if (picked)
        {
            const bool script = picked->category == reflect::TypeCategory::Script;
            auto added = script ? attachScript(editWorld(editor, frame), editor.undoStack(), entity, *picked) : addComponent(editWorld(editor, frame), editor.undoStack(), entity, *picked);
            if (!added)
                log::warn("Cannot {} {}: {}", script ? "attach" : "add", picked->name, added.error());
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
};
}

auto makeInspectorPanel() -> std::unique_ptr<Panel>
{
    return std::make_unique<InspectorPanel>();
}
}
