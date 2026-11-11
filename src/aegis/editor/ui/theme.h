#pragma once

#include <imgui.h>

// The editor's look as named tokens (docs/editor.md, "UI design"): colors and the fonts. The editor takes every color
// from here, never an inline value. The metrics, the style built from these and the icons are in :theme.
namespace aegis::editor::theme
{
// '0xRRGGBB' as the ImVec4 ImGui takes.
constexpr auto rgb(unsigned hex, float alpha = 1.0f) -> ImVec4
{
    return ImVec4{ static_cast<float>((hex >> 16) & 0xFF) / 255.0f, static_cast<float>((hex >> 8) & 0xFF) / 255.0f, static_cast<float>(hex & 0xFF) / 255.0f, alpha };
}

// Dark neutral grays, darkest first: the bars, the panels, the widgets, and the one border.
inline constexpr ImVec4 bgDeep = rgb(0x0E0E0E);
inline constexpr ImVec4 bgWindow = rgb(0x181818);
inline constexpr ImVec4 bgPopup = rgb(0x202020);
inline constexpr ImVec4 bgFrame = rgb(0x272727);
inline constexpr ImVec4 bgFrameHover = rgb(0x333333);
inline constexpr ImVec4 bgFrameActive = rgb(0x404040);
inline constexpr ImVec4 border = rgb(0x2B2B2B);

// The text colors, brightest first. Text reaches 4.5:1 (WCAG 2) on every background it is drawn on, at rest and
// hovered; the ratios are against the bars, the windows, the popups, the frames and a selected row (the accent tint
// over the window). A pressed control is passed over.
// Body text: 15.5, 14.2, 13.1, 12.0, 8.4 on a selected row; hovered 10.1 (frame), 10.9 (row), 5.7 (tab); 5.4 on
// accentFill, 6.7 on selected text.
inline constexpr ImVec4 text = rgb(0xE6E6E6);
// Labels and values that step back: 9.4, 8.7, 7.9, 7.3, 5.1 on a selected row; hovered 6.2 (frame), 6.6 (row).
inline constexpr ImVec4 textDim = rgb(0xB5B5B5);
// Secondary content, ImGui's TextDisabled with it: instance members, engine content, hints, menu shortcuts, a
// zero count. 7.4, 6.8, 6.2, 5.7, 4.8 on a hovered frame, 4.7 on a hovered menu item. A selected row is 4.0: draw
// it there with mutedText.
inline constexpr ImVec4 textMuted = rgb(0xA0A0A0);
// Disabled controls and neutral fills (the W strip, a dragged scrollbar), not text that must be read: 4.9, 4.5,
// 4.1, 3.8. ImGui dims a disabled control by DisabledAlpha instead.
inline constexpr ImVec4 textDisabled = rgb(0x808080);

// Lines, checkmarks, the selection tint, underlines and the active tab. Text on it is 2.8: fill with accentFill.
// The play state has its own colors.
inline constexpr ImVec4 accent = rgb(0x4A8CD9);
inline constexpr ImVec4 accentHover = rgb(0x6AA3E4);
inline constexpr ImVec4 accentActive = rgb(0x3A74B8);

// The fill of filled controls: the default button, active toggles and tools, and its hovered and pressed shades.
// Text on them: 5.4, 4.8, 6.7.
inline constexpr ImVec4 accentFill = rgb(0x2A5D9C);
inline constexpr ImVec4 accentFillHover = rgb(0x3164A4);
inline constexpr ImVec4 accentFillActive = rgb(0x224E85);

// The warning and error text and icons: 13.4 and 7.0 on the bars, 12.3 and 6.4 on the windows, 11.3 and 5.9 on
// the popups.
inline constexpr ImVec4 warning = rgb(0xFFD24D);
inline constexpr ImVec4 error = rgb(0xFF6B6B);

// The tint mixed into the windows while a play session runs, and the frame around the viewport, which also fills
// the Stop button and colors the play state's label (7.3 on the bar); the button's hovered and pressed fills. The
// Stop button's icon is bgDeep: 7.3, 9.1, 5.3.
inline constexpr ImVec4 playTint = rgb(0x733813);
inline constexpr ImVec4 playFrame = rgb(0xE8873A);
inline constexpr ImVec4 playHover = rgb(0xF0A060);
inline constexpr ImVec4 playActive = rgb(0xC96E28);

// The axes' lines, and the inspector's strips along a vector's fields (marks, not text; W's strip is textDisabled).
inline constexpr ImVec4 axisX = rgb(0xE5534B);
inline constexpr ImVec4 axisY = rgb(0x6CC24A);
inline constexpr ImVec4 axisZ = rgb(0x4A8CD9);

// The light and camera icons in the hierarchy: 14.2 and 10.0 on the window, 8.4 and 5.9 on a selected row. In the
// viewport a light's icon takes the light's color, on the backdrop (14.4 for lightIcon over black); iconSelected is
// the ring around a selected icon, a line, not text.
inline constexpr ImVec4 lightIcon = rgb(0xFFE678);
inline constexpr ImVec4 cameraIcon = rgb(0x8CC8FF);
inline constexpr ImVec4 iconBackdrop = rgb(0x1E1E1E, 0.78f);
inline constexpr ImVec4 iconSelected = rgb(0xFFA028);

// The color of secondary content on a row: textMuted, or textDim on a selected row, where textMuted falls below 4.5:1.
// Read-only rows carry the lock icon as well, so the state shows without the color.
constexpr auto mutedText(bool selected) -> ImVec4
{
    return selected ? textDim : textMuted;
}

// The packed color a draw list takes.
inline auto packed(const ImVec4& color) -> ImU32
{
    return ImGui::ColorConvertFloat4ToU32(color);
}

// The factor the metrics are multiplied by: the monitor's content scale times the user's UI scale, as of the last
// applyStyle (1 before it). Anything that draws in pixels, or sizes a window or widget by a number, multiplies by it;
// sizes taken from the style or the font (GetFrameHeight, GetFontSize) already include it.
inline auto scale() -> float
{
    const ImGuiStyle& style = ImGui::GetStyle();
    return style.FontScaleDpi * style.FontScaleMain;
}

// The monitor's part of scale(): the window's content scale as of the last applyStyle.
inline auto contentScale() -> float
{
    return ImGui::GetStyle().FontScaleDpi;
}

// The font families. They stay null (ImGui's default font) until loadFonts found the files.
struct Fonts
{
    ImFont* ui{ nullptr };
    ImFont* mono{ nullptr };
};

inline auto fonts() -> Fonts&
{
    static Fonts instance;
    return instance;
}

// The console's and the IDs' font until it goes out of scope; the size stays the current one.
class MonospaceScope
{
public:
    MonospaceScope() { ImGui::PushFont(fonts().mono, 0.0f); }
    MonospaceScope(const MonospaceScope&) = delete;
    auto operator=(const MonospaceScope&) -> MonospaceScope& = delete;
    ~MonospaceScope() { ImGui::PopFont(); }
};

// A square button showing one of 'icons'; 'id' is unique among the window's buttons. 'active' marks the chosen tool.
inline auto iconButton(const char* icon, const char* id, bool active = false) -> bool
{
    if (active)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, accentFill);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accentFillHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, accentFillActive);
    }
    ImGui::PushID(id);
    const float side = ImGui::GetFrameHeight();
    // No padding: the icon is wider than the square less the frame padding, and ImGui would put it at the left padding
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 0.0f, 0.0f });
    const bool pressed = ImGui::Button(icon, ImVec2{ side, side });
    ImGui::PopStyleVar();
    ImGui::PopID();
    if (active)
        ImGui::PopStyleColor(3);
    return pressed;
}
}
