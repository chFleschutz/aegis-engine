#pragma once

#include <imgui.h>

// The editor's look as named tokens (docs/editor.md, "UI design"): colors, the fonts and the Lucide icons. The editor
// takes every color from here, never an inline value. The metrics and the style built from these are in :theme.
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
inline constexpr ImVec4 text = rgb(0xE6E6E6);
inline constexpr ImVec4 textDim = rgb(0xB5B5B5);
inline constexpr ImVec4 textDisabled = rgb(0x808080);

// Selection and the active tab. The play state has its own colors.
inline constexpr ImVec4 accent = rgb(0x4A8CD9);
inline constexpr ImVec4 accentHover = rgb(0x6AA3E4);
inline constexpr ImVec4 accentActive = rgb(0x3A74B8);

inline constexpr ImVec4 warning = rgb(0xFFD24D);
inline constexpr ImVec4 error = rgb(0xFF6B6B);

// The tint mixed into the windows while a play session runs, and the frame around the viewport.
inline constexpr ImVec4 playTint = rgb(0x733813);
inline constexpr ImVec4 playFrame = rgb(0xE8873A);

inline constexpr ImVec4 axisX = rgb(0xE5534B);
inline constexpr ImVec4 axisY = rgb(0x6CC24A);
inline constexpr ImVec4 axisZ = rgb(0x4A8CD9);

// The light and camera icons in the viewport.
inline constexpr ImVec4 lightIcon = rgb(0xFFE678);
inline constexpr ImVec4 cameraIcon = rgb(0x8CC8FF);
inline constexpr ImVec4 iconBackdrop = rgb(0x1E1E1E, 0.78f);
inline constexpr ImVec4 iconSelected = rgb(0xFFA028);

// The packed color a draw list takes.
inline auto packed(const ImVec4& color) -> ImU32
{
    return ImGui::ColorConvertFloat4ToU32(color);
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
        ImGui::PushStyleColor(ImGuiCol_Button, accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, accentActive);
    }
    ImGui::PushID(id);
    const float side = ImGui::GetFrameHeight();
    const bool pressed = ImGui::Button(icon, ImVec2{ side, side });
    ImGui::PopID();
    if (active)
        ImGui::PopStyleColor(3);
    return pressed;
}

// The Lucide icons the editor uses (lucide.dev, ISC), as UTF-8 for the private-use codepoints of the merged font.
namespace icons
{
inline constexpr const char* select = "\uE1C3"; // mouse-pointer-2
inline constexpr const char* move = "\uE121";   // move
inline constexpr const char* rotate = "\uE2EA"; // rotate-3d
inline constexpr const char* scale = "\uE2EC";  // scaling
inline constexpr const char* local = "\uE061";  // box
inline constexpr const char* world = "\uE0E8";  // globe
inline constexpr const char* snap = "\uE2B5";   // magnet
inline constexpr const char* play = "\uE13C";   // play
inline constexpr const char* pause = "\uE12E";  // pause
inline constexpr const char* stop = "\uE167";   // square
inline constexpr const char* step = "\uE3EA";   // step-forward
inline constexpr const char* camera = "\uE064"; // camera
inline constexpr const char* light = "\uE1C2";  // lightbulb
inline constexpr const char* sun = "\uE178";    // sun
}
}
