module;

#include <imgui.h>
#include <imgui_stdlib.h>

#include "aegis/editor/ui/theme.h"

module aegis.editor;
import std;
import aegis.editor.core;
import aegis.reflect;
import :panels;
import :theme;

namespace aegis::editor
{
auto searchField(const char* id, const char* hint, std::string& text, int flags) -> bool
{
    // The text starts right of the icon: ImGui pads both ends alike, so the padding makes room for it at each
    const ImGuiStyle& style = ImGui::GetStyle();
    const float iconWidth = ImGui::CalcTextSize(theme::icons::search).x;
    const float gap = std::round(style.ItemInnerSpacing.x * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ style.FramePadding.x + iconWidth + gap, style.FramePadding.y });
    const bool changed = ImGui::InputTextWithHint(id, hint, &text, flags);
    ImGui::PopStyleVar();
    const ImVec2 min = ImGui::GetItemRectMin();
    ImGui::GetWindowDrawList()->AddText(ImVec2{ min.x + style.FramePadding.x, min.y + style.FramePadding.y }, theme::packed(theme::textMuted), theme::icons::search);
    return changed;
}

auto entityIcon(EntityKind kind) -> const char*
{
    switch (kind)
    {
    case EntityKind::Camera:
        return theme::icons::camera;
    case EntityKind::DirectionalLight:
        return theme::icons::sun;
    case EntityKind::PointLight:
        return theme::icons::light;
    case EntityKind::Mesh:
        return theme::icons::mesh;
    case EntityKind::SceneInstance:
        return theme::icons::instance;
    case EntityKind::Empty:
        return theme::icons::empty;
    }
    return theme::icons::empty;
}

auto entityIconColor(EntityKind kind) -> std::uint32_t
{
    switch (kind)
    {
    case EntityKind::Camera:
        return theme::packed(theme::cameraIcon);
    case EntityKind::DirectionalLight:
    case EntityKind::PointLight:
        return theme::packed(theme::lightIcon);
    default:
        return theme::packed(theme::textDim);
    }
}

auto componentIcon(const reflect::TypeInfo& type) -> const char*
{
    if (type.category == reflect::TypeCategory::Script)
        return theme::icons::script;
    // The engine's components by their registered names; a game's get the generic icon
    static constexpr std::array<std::pair<std::string_view, const char*>, 10> icons{ {
        { "Transform", theme::icons::transform },
        { "DynamicTag", theme::icons::dynamic },
        { "MainCamera", theme::icons::mainCamera },
        { "Camera", theme::icons::camera },
        { "AmbientLight", theme::icons::ambientLight },
        { "DirectionalLight", theme::icons::sun },
        { "PointLight", theme::icons::light },
        { "MeshRenderer", theme::icons::mesh },
        { "Sky", theme::icons::environment },
        { "SceneInstance", theme::icons::instance },
    } };
    const auto it = std::ranges::find(icons, std::string_view{ type.name }, [](const auto& entry) { return entry.first; });
    return it != icons.end() ? it->second : theme::icons::genericComponent;
}
}
