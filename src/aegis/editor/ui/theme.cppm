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
// The viewport's stats overlay toggle, and the mark of its camera speed field.
inline constexpr const char* stats = lucide::barChart3;
inline constexpr const char* speed = lucide::gauge;
inline constexpr const char* play = lucide::play;
inline constexpr const char* pause = lucide::pause;
inline constexpr const char* stop = lucide::square;
inline constexpr const char* step = lucide::stepForward;
// The menu bar's full viewport button, and the button in full viewport's top bar that leaves it.
inline constexpr const char* fullViewport = lucide::maximize2;
inline constexpr const char* leaveFullViewport = lucide::minimize2;
// The menu bar's button that folds the right column into the side rail, and unfolds it; the rail's panel buttons.
inline constexpr const char* foldColumn = lucide::panelRightClose;
inline constexpr const char* unfoldColumn = lucide::panelRightOpen;
inline constexpr const char* hierarchy = lucide::listTree;
inline constexpr const char* inspector = lucide::slidersHorizontal;
// The other panels' tabs (Assets, Console and Statistics show their buttons' icons).
inline constexpr const char* renderer = lucide::gpu;
inline constexpr const char* profiler = lucide::activity;
inline constexpr const char* allEntities = lucide::list;
inline constexpr const char* camera = lucide::camera;
inline constexpr const char* light = lucide::lightbulb;
inline constexpr const char* sun = lucide::sun;

// The hierarchy's button that opens the Create menu.
inline constexpr const char* create = lucide::plus;

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

// The status bar's drawer buttons, and the drawer's pin and close buttons.
inline constexpr const char* assets = lucide::folder;
inline constexpr const char* console = lucide::terminal;
inline constexpr const char* pin = lucide::pin;
inline constexpr const char* close = lucide::x;

// The status bar's warning and error counts.
inline constexpr const char* warning = lucide::alertTriangle;
inline constexpr const char* error = lucide::circleX;

// Read-only content: the members of a scene instance, the engine content.
inline constexpr const char* locked = lucide::lock;

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
