module aegis.editor:theme;
import std;
import :lucide;

// The editor's look (docs/editor.md, "UI design"): the fonts, the style built from the tokens of theme.h and the
// icons. The tokens are in that header, which the .cpp files include for their ImGui types.
namespace aegis::editor::theme
{
// The Lucide icons the editor uses (lucide.dev, ISC) under the editor's names. :lucide, generated from the font by
// tools/update-lucide.py, has every icon.
namespace icons
{
inline constexpr const char* select = lucide::mousePointer2;
inline constexpr const char* move = lucide::move;
inline constexpr const char* rotate = lucide::rotate3D;
inline constexpr const char* scale = lucide::scaling;
inline constexpr const char* local = lucide::box;
inline constexpr const char* world = lucide::globe;
inline constexpr const char* snap = lucide::magnet;
inline constexpr const char* shown = lucide::eye;
inline constexpr const char* hidden = lucide::eyeOff;
inline constexpr const char* play = lucide::play;
inline constexpr const char* pause = lucide::pause;
inline constexpr const char* stop = lucide::square;
inline constexpr const char* step = lucide::stepForward;
inline constexpr const char* camera = lucide::camera;
inline constexpr const char* light = lucide::lightbulb;
inline constexpr const char* sun = lucide::sun;

// The kinds of entities in the hierarchy (the camera, light and sun above are the rest).
inline constexpr const char* mesh = lucide::cuboid;
inline constexpr const char* instance = lucide::component;
inline constexpr const char* empty = lucide::circleDashed;

// The kinds of assets in the asset list (the mesh above is the static mesh).
inline constexpr const char* material = lucide::palette;
inline constexpr const char* texture = lucide::image;
inline constexpr const char* scene = lucide::clapperboard;
inline constexpr const char* environment = lucide::cloudSun;
inline constexpr const char* model = lucide::boxes;
inline constexpr const char* file = lucide::file;

// The status bar's warning and error counts.
inline constexpr const char* warning = lucide::alertTriangle;
inline constexpr const char* error = lucide::circleX;

// The recovery bar's mark: an autosave of unsaved changes.
inline constexpr const char* recovery = lucide::history;
}

/// @brief The UI font (Inter) with the Lucide icons merged into it, and the monospace font (JetBrains Mono), from
///        'folder' (the engine content's `fonts/`), as ImGui's default font and theme::fonts(). Without the files it
///        warns and keeps ImGui's font. Call it once, before the first frame.
void loadFonts(const std::filesystem::path& folder);

/// @brief Rebuilds ImGui's style from the tokens: the colors, and the metrics times 'contentScale' (the monitor's)
///        times 'uiScale' (the user's). The fonts scale with the same two factors.
void applyStyle(float contentScale, float uiScale);
}
