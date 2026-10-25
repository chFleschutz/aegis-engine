module aegis.editor:theme;
import std;

// The editor's look (docs/editor.md, "UI design"): the fonts and the style built from the tokens of theme.h. The
// tokens and the icons are in that header, which the .cpp files include for their ImGui types.
namespace aegis::editor::theme
{
/// @brief The UI font (Inter) with the Lucide icons merged into it, and the monospace font (JetBrains Mono), from
///        'folder' (the engine content's `fonts/`), as ImGui's default font and theme::fonts(). Without the files it
///        warns and keeps ImGui's font. Call it once, before the first frame.
void loadFonts(const std::filesystem::path& folder);

/// @brief Rebuilds ImGui's style from the tokens: the colors, and the metrics times 'contentScale' (the monitor's)
///        times 'uiScale' (the user's). The fonts scale with the same two factors.
void applyStyle(float contentScale, float uiScale);
}
