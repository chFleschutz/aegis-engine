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

// Drags 'count' values of T, or slides them for a Slider row, within the row's limits.
template<class T>
auto dragValues(const PropertyRow& row, T* values, int count) -> bool
{
    const std::optional<T> min = row.min ? std::optional<T>{ static_cast<T>(*row.min) } : std::nullopt;
    const std::optional<T> max = row.max ? std::optional<T>{ static_cast<T>(*row.max) } : std::nullopt;
    const T* minPtr = min ? &*min : nullptr;
    const T* maxPtr = max ? &*max : nullptr;
    const char* format = std::floating_point<T> ? "%.3f" : nullptr;
    if (row.widget == PropertyWidget::Slider && min && max)
        return ImGui::SliderScalarN("##value", dataType<T>(), values, count, minPtr, maxPtr, format, ImGuiSliderFlags_AlwaysClamp);
    return ImGui::DragScalarN("##value", dataType<T>(), values, count, static_cast<float>(row.step), minPtr, maxPtr, format, ImGuiSliderFlags_AlwaysClamp);
}

template<class T>
auto dragValue(const PropertyRow& row, const reflect::Value& value) -> std::optional<reflect::Value>
{
    T edited = as<T>(value);
    if constexpr (std::same_as<T, glm::vec2> || std::same_as<T, glm::vec3> || std::same_as<T, glm::vec4>)
    {
        if (dragValues(row, &edited.x, row.components))
            return reflect::Value{ edited };
    }
    else if (dragValues(row, &edited, 1))
    {
        return reflect::Value{ edited };
    }
    return std::nullopt;
}

auto drawNumber(const PropertyRow& row, const reflect::Value& value) -> std::optional<reflect::Value>
{
    switch (row.field->kind)
    {
    case reflect::FieldKind::Int: return dragValue<std::int64_t>(row, value);
    case reflect::FieldKind::UInt: return dragValue<std::uint64_t>(row, value);
    case reflect::FieldKind::Float: return dragValue<float>(row, value);
    case reflect::FieldKind::Double: return dragValue<double>(row, value);
    case reflect::FieldKind::Vec2: return dragValue<glm::vec2>(row, value);
    case reflect::FieldKind::Vec3: return dragValue<glm::vec3>(row, value);
    case reflect::FieldKind::Vec4: return dragValue<glm::vec4>(row, value);
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

auto drawColor(const PropertyRow& row, const reflect::Value& value) -> std::optional<reflect::Value>
{
    constexpr ImGuiColorEditFlags flags = ImGuiColorEditFlags_Float;
    if (row.field->kind == reflect::FieldKind::Vec4)
    {
        auto color = as<glm::vec4>(value);
        return ImGui::ColorEdit4("##value", &color.x, flags) ? std::optional{ reflect::Value{ color } } : std::nullopt;
    }
    auto color = as<glm::vec3>(value);
    return ImGui::ColorEdit3("##value", &color.x, flags) ? std::optional{ reflect::Value{ color } } : std::nullopt;
}

auto drawRotation(const PropertyRow& row, const reflect::Value& value) -> std::optional<reflect::Value>
{
    static RotationDrag s_drag;
    const ImGuiID id = ImGui::GetID("##value");
    glm::vec3 euler = s_drag.id == id ? s_drag.euler : glm::degrees(glm::eulerAngles(as<glm::quat>(value)));
    const bool changed = dragValues(row, &euler.x, 3);
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

// The name of the asset 'id' as the slot shows it: its file and, for a part of one, its sub-asset key.
auto assetName(const assets::AssetDatabase& database, std::uint64_t id) -> std::string
{
    if (id == 0)
        return "None";
    const auto* entry = database.find(assets::AssetId{ id });
    if (!entry)
        return std::format("{} (not in the project)", assets::AssetId{ id });
    return entry->key.empty() ? entry->path : std::format("{} : {}", entry->path, entry->key);
}

// The asset's name, and a picker of the assets of the field's type (and None). An 'editable' slot takes an asset of
// its type dragged from the asset browser.
auto drawAssetSlot(const PropertyRow& row, const reflect::Value& value, const assets::AssetDatabase& database, bool editable) -> std::optional<reflect::Value>
{
    static std::string s_filter;
    const auto current = as<std::uint64_t>(value);
    std::optional<reflect::Value> edited;
    const bool open = ImGui::BeginCombo("##value", assetName(database, current).c_str(), ImGuiComboFlags_HeightLarge);
    if (!open && editable && ImGui::BeginDragDropTarget())
    {
        const auto& slotType = row.field->assetType;
        if (const auto dropped = acceptAssetPayload([&slotType](const AssetPayload& payload) { return acceptsAsset(slotType, payload); }))
            edited = reflect::Value{ dropped->id };
        ImGui::EndDragDropTarget();
    }
    if (open)
    {
        if (ImGui::IsWindowAppearing())
        {
            s_filter.clear();
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##filter", "Search", &s_filter);
        if (ImGui::Selectable("None", current == 0))
            edited = reflect::Value{ std::uint64_t{ 0 } };
        for (const auto& entry : database.entries())
        {
            if (entry.type != row.field->assetType)
                continue;
            const auto name = assetName(database, entry.id.value);
            if (!s_filter.empty() && name.find(s_filter) == std::string::npos)
                continue;
            ImGui::PushID(static_cast<int>(entry.id.value ^ (entry.id.value >> 32)));
            if (ImGui::Selectable(name.c_str(), entry.id.value == current))
                edited = reflect::Value{ entry.id.value };
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", row.field->assetType.c_str());
    return edited;
}

// The row's widget for 'value'; the new value when it was edited this frame.
auto drawValue(const PropertyRow& row, const reflect::Value& value, const assets::AssetDatabase& database, bool editable) -> std::optional<reflect::Value>
{
    switch (row.widget)
    {
    case PropertyWidget::Checkbox:
    {
        bool checked = as<bool>(value);
        return ImGui::Checkbox("##value", &checked) ? std::optional{ reflect::Value{ checked } } : std::nullopt;
    }
    case PropertyWidget::Drag:
    case PropertyWidget::Slider: return drawNumber(row, value);
    case PropertyWidget::Angle: return drawAngle(row, value);
    case PropertyWidget::Color: return drawColor(row, value);
    case PropertyWidget::Rotation: return drawRotation(row, value);
    case PropertyWidget::Combo: return drawCombo(row, value);
    case PropertyWidget::Text: return drawText(value);
    case PropertyWidget::AssetSlot: return drawAssetSlot(row, value, database, editable);
    }
    return std::nullopt;
}

// The registered fields of 'object' (the entity's component or script of 'type') as a two-column property grid.
// Each edit is recorded in the undo stack; a widget still being dragged or typed in extends one entry.
void drawFields(const reflect::TypeInfo& type, void* object, world::Entity entity, std::optional<std::size_t> scriptIndex, bool readOnly, EditorContext& editor, FrameContext& frame)
{
    const auto rows = propertyRows(type);
    if (rows.empty() || !ImGui::BeginTable("##fields", 2, ImGuiTableFlags_SizingStretchProp))
        return;
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.4f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.6f);

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
        const auto edited = drawValue(row, current, frame.assets.database(), !readOnly && !row.readOnly);
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
        const std::string label{ type->displayName() };
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

// A header per type: open with its fields, a leaf for a tag (no fields), with the ⋮ button and a right click opening its
// menu. Returns false when the menu asked to remove the component or detach the script.
auto drawSection(const reflect::TypeInfo& type, void* object, world::Entity entity, std::optional<std::size_t> scriptIndex, bool readOnly, EditorContext& editor, FrameContext& frame) -> bool
{
    const std::string title{ type.displayName() };
    const ImGuiTreeNodeFlags flags = (type.fields.empty() ? ImGuiTreeNodeFlags_Leaf : ImGuiTreeNodeFlags_DefaultOpen) | ImGuiTreeNodeFlags_AllowOverlap;
    const bool open = ImGui::CollapsingHeader(title.c_str(), flags);
    bool menuRequested = ImGui::IsItemClicked(ImGuiMouseButton_Right);

    // The button sits at the right end of the header's row, over the header
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float side = max.y - min.y;
    ImGui::SetCursorScreenPos(ImVec2{ max.x - side, min.y });
    menuRequested |= menuButton("##menu", side);
    if (menuRequested)
        ImGui::OpenPopup("##section");
    const auto action = drawSectionMenu(type, object, entity, scriptIndex, readOnly, editor, frame);

    if (open)
        drawFields(type, object, entity, scriptIndex, readOnly, editor, frame);
    ImGui::Spacing();
    return action != SectionAction::Remove;
}

// The selected entity: its name (the ID in a tooltip), its registered components and its scripts, each generated from the type
// registry, and the Add menu. Edits apply directly: the panel runs at the frame's safe point.
class InspectorPanel final : public Panel
{
public:
    InspectorPanel() { setOpen(true); }

    [[nodiscard]] auto name() const -> std::string_view override { return "Inspector"; }

    void draw(EditorContext& editor, FrameContext& frame) override
    {
        const auto entity = editor.selectedEntity();
        if (!ImGui::Begin("Inspector") || !entity)
        {
            ImGui::End();
            return;
        }

        auto& registry = editor.world()->registry();
        ImGui::PushID(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(registry.get<StableId>(entity).value)));
        // A scene instance's entities are not saved (the holder's SceneInstance recreates them): an edit would be lost
        const bool readOnly = registry.has<serialize::InstanceMember>(entity);
        drawHeader(registry, entity, readOnly, editor, frame);
        drawComponents(registry, entity, readOnly, editor, frame);
        drawScripts(registry, entity, readOnly, editor, frame);
        if (!readOnly)
            drawAddMenu(registry, entity, editor, frame);
        ImGui::PopID();
        ImGui::End();
    }

private:
    static void drawHeader(world::Registry& registry, world::Entity entity, bool readOnly, EditorContext& editor, FrameContext& frame)
    {
        const bool rename = editor.takeRenameRequest();
        const auto id = std::format("{:016x}", registry.get<StableId>(entity).value);
        const float side = ImGui::GetFrameHeight();
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
        if (ImGui::Button("Add...", ImVec2{ -FLT_MIN, 0.0f }))
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
        const bool confirmed = ImGui::InputTextWithHint("##filter", "Search", &s_filter, ImGuiInputTextFlags_EnterReturnsTrue);

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
