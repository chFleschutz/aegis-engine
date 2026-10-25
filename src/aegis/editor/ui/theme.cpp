module;

#include <imgui.h>

#include "aegis/editor/ui/theme.h"

module aegis.editor;
import std;
import aegis.core;
import :theme;

namespace aegis::editor::theme
{
namespace
{
constexpr float fontSize{ 15.0f };    // Before the scales
constexpr float iconOffsetX{ -3.0f }; // Centers the icons in their buttons and on the text line
constexpr float iconOffsetY{ 2.0f };

// The path as the UTF-8 ImGui opens files with.
auto utf8(const std::filesystem::path& path) -> std::string
{
    const auto text = path.u8string();
    return std::string{ text.begin(), text.end() };
}

void setColors(ImGuiStyle& style)
{
    auto* c = style.Colors;
    const auto dim = [](const ImVec4& color, float alpha) { return ImVec4{ color.x, color.y, color.z, alpha }; };
    const ImVec4 clear{ 0.0f, 0.0f, 0.0f, 0.0f };

    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = textDisabled;
    c[ImGuiCol_WindowBg] = bgWindow;
    c[ImGuiCol_ChildBg] = clear;
    c[ImGuiCol_PopupBg] = bgPopup;
    c[ImGuiCol_Border] = border;
    c[ImGuiCol_BorderShadow] = clear;
    c[ImGuiCol_FrameBg] = bgFrame;
    c[ImGuiCol_FrameBgHovered] = bgFrameHover;
    c[ImGuiCol_FrameBgActive] = bgFrameActive;
    c[ImGuiCol_TitleBg] = bgDeep;
    c[ImGuiCol_TitleBgActive] = bgDeep;
    c[ImGuiCol_TitleBgCollapsed] = bgDeep;
    c[ImGuiCol_MenuBarBg] = bgDeep;
    c[ImGuiCol_ScrollbarBg] = clear;
    c[ImGuiCol_ScrollbarGrab] = bgFrameHover;
    c[ImGuiCol_ScrollbarGrabHovered] = bgFrameActive;
    c[ImGuiCol_ScrollbarGrabActive] = textDisabled;
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accentActive;
    c[ImGuiCol_Button] = bgFrame;
    c[ImGuiCol_ButtonHovered] = bgFrameHover;
    c[ImGuiCol_ButtonActive] = bgFrameActive;
    c[ImGuiCol_Header] = dim(accent, 0.35f); // Selected rows
    c[ImGuiCol_HeaderHovered] = dim(accent, 0.2f);
    c[ImGuiCol_HeaderActive] = dim(accent, 0.5f);
    c[ImGuiCol_Separator] = border;
    c[ImGuiCol_SeparatorHovered] = accentHover;
    c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_ResizeGrip] = dim(accent, 0.2f);
    c[ImGuiCol_ResizeGripHovered] = dim(accent, 0.6f);
    c[ImGuiCol_ResizeGripActive] = accent;
    c[ImGuiCol_TabHovered] = dim(accent, 0.6f);
    c[ImGuiCol_Tab] = bgDeep;
    c[ImGuiCol_TabSelected] = bgWindow;
    c[ImGuiCol_TabSelectedOverline] = clear;
    c[ImGuiCol_TabDimmed] = bgDeep;
    c[ImGuiCol_TabDimmedSelected] = bgWindow;
    c[ImGuiCol_TabDimmedSelectedOverline] = clear;
    c[ImGuiCol_DockingPreview] = dim(accent, 0.5f);
    c[ImGuiCol_DockingEmptyBg] = bgDeep;
    c[ImGuiCol_PlotLines] = textDim;
    c[ImGuiCol_PlotLinesHovered] = accentHover;
    c[ImGuiCol_PlotHistogram] = accent;
    c[ImGuiCol_PlotHistogramHovered] = accentHover;
    c[ImGuiCol_TableHeaderBg] = bgFrame;
    c[ImGuiCol_TableBorderStrong] = border;
    c[ImGuiCol_TableBorderLight] = dim(border, 0.5f);
    c[ImGuiCol_TableRowBg] = clear;
    c[ImGuiCol_TableRowBgAlt] = ImVec4{ 1.0f, 1.0f, 1.0f, 0.03f };
    c[ImGuiCol_TextLink] = accentHover;
    c[ImGuiCol_TextSelectedBg] = dim(accent, 0.4f);
    c[ImGuiCol_DragDropTarget] = accentHover;
    c[ImGuiCol_NavCursor] = accent;
    c[ImGuiCol_NavWindowingHighlight] = dim(text, 0.7f);
    c[ImGuiCol_NavWindowingDimBg] = ImVec4{ 0.0f, 0.0f, 0.0f, 0.5f };
    c[ImGuiCol_ModalWindowDimBg] = ImVec4{ 0.0f, 0.0f, 0.0f, 0.5f };
}

// The metrics at 100%.
void setMetrics(ImGuiStyle& style)
{
    style.WindowPadding = ImVec2{ 8.0f, 8.0f };
    style.FramePadding = ImVec2{ 6.0f, 4.0f };
    style.ItemSpacing = ImVec2{ 8.0f, 4.0f };
    style.ItemInnerSpacing = ImVec2{ 6.0f, 4.0f };
    style.CellPadding = ImVec2{ 6.0f, 3.0f };
    style.TouchExtraPadding = ImVec2{ 2.0f, 2.0f };
    style.IndentSpacing = 16.0f;
    style.ScrollbarSize = 10.0f; // Thin
    style.GrabMinSize = 10.0f;

    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBarBorderSize = 0.0f;
    style.TabBarOverlineSize = 0.0f; // No line over the selected tab
    style.DockingSeparatorSize = 2.0f;

    style.WindowRounding = 0.0f; // Docked windows are square
    style.ChildRounding = 3.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 3.0f;
}
}

void loadFonts(const std::filesystem::path& folder)
{
    const auto ui = folder / "Inter-Regular.ttf";
    const auto icons = folder / "Lucide.ttf";
    const auto mono = folder / "JetBrainsMono-Regular.ttf";
    std::error_code error;
    if (!std::filesystem::exists(ui, error) || !std::filesystem::exists(icons, error) || !std::filesystem::exists(mono, error))
    {
        log::warn("The editor fonts are not in '{}': using ImGui's font", utf8(folder));
        return;
    }

    // Inter has alternate glyphs in the private-use area, where Lucide has its icons: Lucide must win there
    static constexpr std::array<ImWchar, 3> privateUse{ 0xE000, 0xF8FF, 0 };

    ImGuiIO& io = ImGui::GetIO();
    auto& loaded = fonts();
    ImFontConfig base;
    base.GlyphExcludeRanges = privateUse.data();
    loaded.ui = io.Fonts->AddFontFromFileTTF(utf8(ui).c_str(), fontSize, &base);
    if (loaded.ui)
    {
        ImFontConfig merge;
        merge.MergeMode = true;
        merge.PixelSnapH = true;
        merge.GlyphOffset = ImVec2{ iconOffsetX, iconOffsetY };
        io.Fonts->AddFontFromFileTTF(utf8(icons).c_str(), fontSize, &merge);
        io.FontDefault = loaded.ui;
    }
    loaded.mono = io.Fonts->AddFontFromFileTTF(utf8(mono).c_str(), fontSize);
}

void applyStyle(float contentScale, float uiScale)
{
    ImGuiStyle style;
    setColors(style);
    setMetrics(style);
    style.ScaleAllSizes(contentScale * uiScale);
    style.FontSizeBase = fontSize;
    style._NextFrameFontSizeBase = fontSize; // Takes effect in the frame that is already running
    style.FontScaleDpi = contentScale;
    style.FontScaleMain = uiScale;
    ImGui::GetStyle() = style;
}
}
