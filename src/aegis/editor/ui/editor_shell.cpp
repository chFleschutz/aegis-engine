module;

#include <cfloat>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <ImGuizmo.h>

#include "aegis/editor/ui/theme.h"

module aegis.editor;
import std;
import aegis.assets;
import aegis.core;
import aegis.paths;
import aegis.reflect;
import aegis.serialize;
import aegis.world;
import :actions;
import :panels;
import :theme;

namespace aegis::editor
{
namespace
{
// A window over the whole display, behind the panels, so mouse events reach the editor everywhere.
void beginMainWindow()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                         ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
    ImGui::Begin("Main Window", nullptr, windowFlags);
    ImGui::PopStyleVar(3);
}

// Tints the windows while a play session runs, so nobody mistakes the play world for the edit world. Returns how
// many colors to pop.
auto pushPlayTint() -> int
{
    const ImVec4 tint = theme::playTint;
    constexpr std::array tinted{ ImGuiCol_WindowBg, ImGuiCol_MenuBarBg, ImGuiCol_TitleBg, ImGuiCol_TitleBgActive, ImGuiCol_TitleBgCollapsed };
    for (const auto color : tinted)
    {
        const ImVec4 base = ImGui::GetStyle().Colors[color];
        ImGui::PushStyleColor(color, ImVec4{ std::lerp(base.x, tint.x, 0.35f), std::lerp(base.y, tint.y, 0.35f), std::lerp(base.z, tint.z, 0.35f), base.w });
    }
    return static_cast<int>(tinted.size());
}

// The layout file of 'project' as UTF-8, which ImGui expects; its folder is created. Empty if it can't be.
auto layoutFile(const std::filesystem::path& project) -> std::string
{
    const auto folder = project / ".aegis" / "editor";
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    if (error)
    {
        log::warn("The editor layout is not kept: cannot create '{}' ({})", folder.string(), error.message());
        return {};
    }
    const auto file = (folder / "imgui.ini").u8string();
    return std::string{ file.begin(), file.end() };
}

// The scene file at 'path' in one of the project's (writable) roots, if the database has it and it is still there.
auto findProjectScene(const assets::AssetDatabase& database, std::string_view path) -> std::optional<assets::AssetId>
{
    for (std::size_t root = 0; root < database.roots().size(); ++root)
    {
        if (database.roots()[root].readOnly)
            continue;
        const auto id = database.find(root, path);
        const auto* entry = id ? database.find(*id) : nullptr;
        std::error_code error;
        if (entry && entry->type == "Scene" && std::filesystem::exists(database.absolutePath(*entry), error))
            return id;
    }
    return std::nullopt;
}

// The default layout in the dockspace 'dockspace' of 'size': one column on the right over the full height, the
// hierarchy above the inspector, and the viewport in the rest. The column is about 360 px wide (scaled, at most 30%
// of the window) and keeps that width when the window is resized: ImGui gives the change to the central node. The
// drawer panels (assets, console, the debug panels) have no node: they open in the drawer, and a node below the
// viewport is split off when one is pinned (EditorShell::pinNode).
void buildDefaultLayout(ImGuiID dockspace, ImVec2 size)
{
    constexpr float columnWidth = 360.0f; // At 100% scale
    constexpr float maxColumnShare = 0.3f;
    constexpr float hierarchyShare = 0.4f; // Of the column's height

    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, size);

    ImGuiID center = dockspace;
    const float columnShare = std::min(columnWidth * theme::scale() / std::max(size.x, 1.0f), maxColumnShare);
    ImGuiID inspector = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, columnShare, nullptr, &center);
    const ImGuiID hierarchy = ImGui::DockBuilderSplitNode(inspector, ImGuiDir_Up, hierarchyShare, nullptr, &inspector);

    const auto dock = [](std::initializer_list<const char*> windows, ImGuiID node) {
        for (const char* window : windows)
            ImGui::DockBuilderDockWindow(window, node);
        ImGui::DockBuilderGetNode(node)->SelectedTabId = ImGui::GetID(*windows.begin()); // The first one shows
    };
    dock({ "Hierarchy" }, hierarchy);
    dock({ "Inspector" }, inspector);
    dock({ "Viewport" }, center);
    ImGui::DockBuilderFinish(dockspace);
}

// The share of the viewport's node the node of the pinned panels takes when it is split off.
constexpr float pinnedShare = 0.3f;

// Whether 'point' is inside 'rect'.
auto contains(const std::optional<Viewport::Rect>& rect, ImVec2 point) -> bool
{
    return rect && point.x >= rect->x && point.y >= rect->y && point.x < rect->x + rect->width && point.y < rect->y + rect->height;
}

// Whether the window 'title' floats now: it exists and is docked nowhere.
auto floats(std::string_view title) -> bool
{
    const std::string name{ title };
    const ImGuiWindow* window = ImGui::FindWindowByName(name.c_str());
    return window && window->DockId == 0;
}

// The dock node the window 'title' is docked in, or was docked in last (its settings, before it is first drawn); 0 for none.
auto dockIdOf(std::string_view title) -> ImGuiID
{
    const std::string name{ title };
    if (const ImGuiWindow* window = ImGui::FindWindowByName(name.c_str()))
        return window->DockId;
    const ImGuiWindowSettings* settings = ImGui::FindWindowSettingsByID(ImHashStr(name.c_str()));
    return settings ? settings->DockId : 0;
}
}

EditorShell::EditorShell()
{
    // The console sees every message from here on, and the log still writes to stderr.
    m_logSink = log::addSink([this](log::Level level, std::string_view message) { m_context.console().push(level, message); });

    m_panels.push_back(makeScenePanel(m_actions));
    m_panels.push_back(makeInspectorPanel());
    m_panels.push_back(makeAssetPanel());
    m_panels.push_back(makeConsolePanel());
    m_panels.push_back(makeRendererPanel());
    m_panels.push_back(makeStatisticsPanel());
    m_panels.push_back(makeProfilerPanel());
    m_panels.push_back(makeAllEntitiesPanel());
    m_panels.push_back(makeDemoPanel());
    m_preferencesWindow = makePreferencesPanel();
    m_shortcutsWindow = makeShortcutsPanel(m_actions);
    m_aboutWindow = makeAboutPanel();
    for (const auto& panel : m_panels)
    {
        if (panel->isOpen())
            m_defaultOpen.push_back(panel.get());
    }
    buildActions();
    m_viewport.setReleaseHint(std::format("{} releases the input", m_actions.shortcut("play.release")));
}

EditorShell::~EditorShell()
{
    log::removeSink(m_logSink);
}

auto EditorShell::onStartup(Engine& engine) -> std::expected<void, EngineError>
{
    m_engine = &engine;
    m_scene = engine.scene();
    theme::loadFonts(paths::content() / "fonts");
    // A drag in a window's body is the viewport's (orbit, gizmo), not a window move
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    if (engine.desc().editorPanels == "all")
    {
        for (const auto& panel : m_panels)
        {
            panel->setOpen(panel->docked()); // Not the ImGui demo: it floats over the viewport
            if (panel->drawer())
                pin(panel->name()); // The drawer shows only one
        }
        m_focusPinned = "Assets"; // Its tab shows
    }
    m_context.setWorld(engine.world()); // Now, so the first frame keeps a selection made here
    if (const auto& name = engine.desc().editorSelect; !name.empty())
        selectByName(name);
    m_playAfterFirstFrame = engine.desc().editorPlay;
    if (engine.desc().editorFullViewport)
        toggleFullViewport();
    // A headless run (a test, a screenshot) neither reads nor changes the user's layout.
    if (!engine.headless() && !engine.project().empty())
    {
        m_layoutFile = layoutFile(engine.project());
        m_recentFile = recentScenesPath(engine.project());
        m_autosaveFolder = autosaveFolder(engine.project());
        if (auto loaded = loadRecentScenes(m_recentFile))
            m_recent = std::move(*loaded);
        else
            log::warn("The recent scenes are reset: {}", loaded.error());
    }
    addSettingsHandler(); // Before the layout file is read, at the first frame
    ImGui::GetIO().IniFilename = m_layoutFile.empty() ? nullptr : m_layoutFile.c_str();

    // Likewise the preferences: a headless run neither reads nor changes them.
    if (!engine.headless())
    {
        m_preferencesFile = preferencesPath();
        if (m_preferencesFile.empty())
            log::warn("The editor preferences are not kept: no user config folder");
        else if (auto loaded = loadPreferences(m_preferencesFile))
            m_context.preferences() = std::move(*loaded);
        else
            log::warn("The editor preferences are reset: {}", loaded.error());
        m_savedPreferences = m_context.preferences(); // What the file holds
        if (!engine.project().empty())
            m_context.preferences().addRecentProject(engine.project());
        savePreferencesIfChanged();
    }
    rememberScene(engine.assets().database());
    offerRecovery(engine.assets().database());
    updateStyle();
    return {};
}

void EditorShell::onShutdown()
{
    savePreferencesIfChanged();
    // ImGui would save on destroying its context, after this shell (and the file name) are gone.
    if (!m_layoutFile.empty())
    {
        ImGui::SaveIniSettingsToDisk(m_layoutFile.c_str());
        ImGui::GetIO().IniFilename = nullptr;
    }
    ImGui::RemoveSettingsHandler("Aegis"); // It points at this shell
}

void EditorShell::addSettingsHandler()
{
    // A section of its own in the layout file: "[Aegis][Drawers]" with the line Drawers::settingsLine writes
    ImGuiSettingsHandler handler;
    handler.TypeName = "Aegis";
    handler.TypeHash = ImHashStr("Aegis");
    handler.UserData = this;
    handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler* self, const char* name) -> void* {
        return std::string_view{ name } == "Drawers" ? self->UserData : nullptr;
    };
    handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line) {
        static_cast<EditorShell*>(entry)->m_drawers.readSettingsLine(line);
    };
    handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* self, ImGuiTextBuffer* buffer) {
        const auto line = static_cast<EditorShell*>(self->UserData)->m_drawers.settingsLine();
        buffer->appendf("[Aegis][Drawers]\n%s\n\n", line.c_str());
    };
    ImGui::AddSettingsHandler(&handler);
}

auto EditorShell::renderView() const -> std::optional<RenderView>
{
    return m_viewport.renderView();
}

auto EditorShell::canClose() -> bool
{
    if (!m_context.unsaved())
        return true;
    if (!m_pending)
    {
        m_pending = PendingAction{ .kind = PendingAction::Kind::Close };
        m_openPrompt = true;
    }
    return false;
}

auto EditorShell::inputCapture() const -> InputCapture
{
    const auto& io = ImGui::GetIO();
    // The play world's game has it, but for the key of an editor chord until it is released
    if (m_viewport.givesInputToGame())
        return { .keyboard = m_actions.chordHeld() };
    const bool viewport = m_viewport.capturesInput();
    return { .keyboard = io.WantCaptureKeyboard || viewport, .mouse = io.WantCaptureMouse || viewport };
}

void EditorShell::onFrame(FrameContext& frame)
{
    if (!m_context.playing())
        m_context.setWorld(frame.world);
    m_context.selection().prune(m_context.world()->registry());
    // An edit instead of Restore: the autosave stays until the next one replaces it
    if (m_recovery && m_context.unsaved())
        m_recovery.reset();

    drawUi(frame);
    updateWorlds(frame);
    // --editor-play: the first frame shows edit mode, the next ones play
    if (std::exchange(m_playAfterFirstFrame, false))
        startPlay(frame);
    updateTitle();
    updateAutosave(frame);

    // Not while a value is being dragged: that would write the file every frame
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        savePreferencesIfChanged();
}

void EditorShell::drawUi(FrameContext& frame)
{
    const ScopeProfiler profiler{ "Editor UI" };
    updateStyle();
    ImGuizmo::BeginFrame();

    const int tint = m_context.playing() ? pushPlayTint() : 0;
    beginMainWindow();
    drawDragTooltip(frame); // First, so a drop target's own tooltip replaces it
    m_actions.handleShortcuts(frame, m_viewport.givesInputToGame());
    if (m_context.renameRequested())
    {
        openPanel("Inspector"); // The name field is there
        if (m_fullViewport.active())
        {
            m_fullViewport.leave();
            syncFullViewport();
        }
    }
    // In full viewport the menu bar, the status bar and the floating windows are not drawn. The dockspace and the
    // docked panels still are, below the viewport's window: a docked window that isn't submitted leaves its node, and
    // a node that loses its last window is deleted, so the layout would not come back.
    const bool full = m_fullViewport.active();
    if (!full)
        drawMenuBar(frame);
    drawDockspace();
    for (const auto& panel : m_panels)
    {
        if (!panel->isOpen() || (full && (!panel->docked() || floats(panel->name()))))
            continue;
        if (panel->column())
            drawColumnPanel(*panel, frame);
        else if (!panel->drawer())
            panel->draw(m_context, frame);
        else if (m_drawers.isPinned(panel->name()))
            drawPinned(*panel, frame);
    }
    for (auto* window : { m_preferencesWindow.get(), m_shortcutsWindow.get(), m_aboutWindow.get() })
    {
        if (window->isOpen() && !full)
            window->draw(m_context, frame);
    }
    std::function<void()> bar;
    if (m_recovery)
        bar = [this] { drawRecoveryBar(); };
    std::function<void()> topBar;
    if (full)
        topBar = [this, &frame] { drawFullViewportBar(frame); };
    m_viewport.draw(m_context, frame, bar, topBar);
    const bool drawerWasShown = m_drawers.shown().has_value();
    drawDrawer(frame); // Over the viewport, whose image it is placed by
    leaveFullViewportOnEscape(drawerWasShown);
    switch (std::exchange(m_recoveryChoice, RecoveryChoice::None))
    {
    case RecoveryChoice::Restore:
        restoreAutosave(frame);
        break;
    case RecoveryChoice::Discard:
        deleteAutosave(m_scene);
        break;
    case RecoveryChoice::None:
        break;
    }
    if (const auto scene = m_context.takeSceneRequest())
        request({ .kind = PendingAction::Kind::Open, .scene = assets::AssetId{ *scene } }, frame);
    if (!m_fullViewport.active())
    {
        drawStatusBar(frame);
        drawStyleEditor();
    }
    drawOpenPopup(frame);
    drawSaveAsPopup(frame);
    drawUnsavedPrompt(frame);
    ImGui::End();
    ImGui::PopStyleColor(tint);
}

void EditorShell::request(PendingAction action, FrameContext& frame)
{
    if (m_context.unsaved())
    {
        m_pending = action;
        m_openPrompt = true;
    }
    else
        run(action, frame);
}

void EditorShell::run(PendingAction action, FrameContext& frame)
{
    if (action.kind == PendingAction::Kind::Close)
    {
        frame.engine.quit();
        return;
    }
    stopPlay(frame);
    if (action.kind == PendingAction::Kind::New)
        frame.engine.newScene();
    else if (auto loaded = frame.engine.loadScene(action.scene); !loaded)
        log::warn("Cannot open the scene: {}", describe(loaded.error()));
    m_scene = frame.engine.scene();
    rememberScene(frame.assets.database());
    m_context.resetScene();
    m_viewport.resetCamera();
    offerRecovery(frame.assets.database());
}

auto EditorShell::save(FrameContext& frame) -> bool
{
    const auto* entry = m_scene ? frame.assets.database().find(*m_scene) : nullptr;
    if (!entry)
    {
        m_openSaveAs = true;
        return false;
    }
    const auto file = frame.assets.database().absolutePath(*entry);
    if (auto written = writeSceneFile(m_context.editWorld()->registry(), frame.types, m_scene->value, file); !written)
    {
        log::warn("Cannot save the scene: {}", written.error());
        return false;
    }
    m_context.markSaved();
    deleteAutosave(m_scene);
    rememberScene(frame.assets.database());
    return true;
}

auto EditorShell::saveAs(FrameContext& frame, std::string_view typed) -> std::expected<void, std::string>
{
    const auto path = sceneFilePath(typed);
    if (!path)
        return std::unexpected{ path.error() };
    const auto& database = frame.assets.database();
    const auto file = database.roots().front().path / std::filesystem::path{ std::u8string{ path->begin(), path->end() } };
    std::error_code error;
    if (std::filesystem::exists(file, error))
        return std::unexpected{ std::format("'{}' exists already", *path) };
    std::filesystem::create_directories(file.parent_path(), error);
    if (error)
        return std::unexpected{ std::format("Cannot create the folder of '{}': {}", *path, error.message()) };
    const auto id = assets::AssetId::generate();
    const auto before = m_scene; // Its autosave holds the changes this saves
    if (auto written = writeSceneFile(m_context.editWorld()->registry(), frame.types, id.value, file); !written)
        return std::unexpected{ written.error() };
    auto added = frame.assets.addFile(*path);
    if (!added)
        return std::unexpected{ std::format("Saved, but the asset database does not take it: {}", added.error()) };
    m_scene = *added;
    m_context.markSaved();
    deleteAutosave(before);
    rememberScene(database);
    return {};
}

auto EditorShell::sceneFileName() const -> std::string
{
    const auto* entry = m_scene && m_engine ? m_engine->assets().database().find(*m_scene) : nullptr;
    return entry ? std::filesystem::path{ entry->path }.filename().string() : std::string{ "Untitled" };
}

auto EditorShell::sceneName() const -> std::string
{
    return std::format("{}{}", sceneFileName(), m_context.unsaved() ? "*" : "");
}

void EditorShell::updateTitle()
{
    auto title = std::format("{} - Aegis Editor", sceneName());
    if (title == m_title || !m_engine)
        return;
    m_title = std::move(title);
    m_engine->setTitle(m_title);
}

void EditorShell::drawFileMenu(FrameContext& frame)
{
    if (!ImGui::BeginMenu("File"))
        return;
    m_actions.menuItem("file.new", frame);
    m_actions.menuItem("file.open", frame);
    drawOpenRecentMenu(frame);
    ImGui::Separator();
    m_actions.menuItem("file.save", frame);
    m_actions.menuItem("file.saveAs", frame);
    ImGui::EndMenu();
}

void EditorShell::drawOpenRecentMenu(FrameContext& frame)
{
    const auto& database = frame.assets.database();
    if (m_recent.prune([&database](std::string_view path) { return findProjectScene(database, path).has_value(); }))
        saveRecentScenes();
    if (!ImGui::BeginMenu("Open Recent", !m_recent.paths.empty()))
        return;
    std::optional<assets::AssetId> chosen;
    for (const auto& path : m_recent.paths)
    {
        if (ImGui::MenuItem(path.c_str(), nullptr, m_scene && findProjectScene(database, path) == m_scene))
            chosen = findProjectScene(database, path);
    }
    ImGui::EndMenu();
    if (chosen)
        request({ .kind = PendingAction::Kind::Open, .scene = *chosen }, frame);
}

void EditorShell::rememberScene(const assets::AssetDatabase& database)
{
    const auto* entry = m_scene ? database.find(*m_scene) : nullptr;
    if (!entry || database.roots()[entry->root].readOnly)
        return;
    m_recent.add(entry->path);
    saveRecentScenes();
}

void EditorShell::saveRecentScenes()
{
    if (m_recentFile.empty())
        return;
    if (const auto saved = aegis::editor::saveRecentScenes(m_recent, m_recentFile); !saved)
        log::warn("The recent scenes are not saved: {}", saved.error());
}

void EditorShell::drawOpenPopup(FrameContext& frame)
{
    if (std::exchange(m_openOpen, false))
    {
        m_openSearch.clear();
        m_openSelection = m_scene;
        ImGui::OpenPopup("Open Scene");
    }
    ImGui::SetNextWindowSize(ImVec2{ 420.0f * theme::scale(), 360.0f * theme::scale() }, ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Open Scene"))
        return;
    // The arrow keys move the selection, not the keyboard focus: the popup owns them, so ImGui's keyboard navigation
    // (which reads only keys nobody owns) leaves them alone from the next frame on
    const ImGuiID arrowsOwner = ImGui::GetID("##arrows");
    ImGui::SetKeyOwner(ImGuiKey_UpArrow, arrowsOwner);
    ImGui::SetKeyOwner(ImGuiKey_DownArrow, arrowsOwner);
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##search", "Search", &m_openSearch);

    const auto& database = frame.assets.database();
    std::vector<const assets::AssetEntry*> scenes;
    for (const auto& entry : database.entries())
    {
        if (entry.type == "Scene" && !database.roots()[entry.root].readOnly && core::containsIgnoringCase(entry.path, m_openSearch))
            scenes.push_back(&entry);
    }
    std::ranges::sort(scenes, {}, &assets::AssetEntry::path);

    // The selection is one of the listed scenes: a search selects the first match, the arrow keys move it
    auto selected = std::ranges::find_if(scenes, [this](const assets::AssetEntry* entry) { return m_openSelection == entry->id; });
    if (selected == scenes.end())
        selected = scenes.begin();
    const bool down = ImGui::IsKeyPressed(ImGuiKey_DownArrow, ImGuiInputFlags_Repeat, arrowsOwner) && selected != scenes.end() && std::next(selected) != scenes.end();
    const bool up = ImGui::IsKeyPressed(ImGuiKey_UpArrow, ImGuiInputFlags_Repeat, arrowsOwner) && selected != scenes.begin();
    if (down)
        ++selected;
    else if (up)
        --selected;
    const bool moved = down || up;
    m_openSelection = selected != scenes.end() ? std::optional{ (*selected)->id } : std::nullopt;

    std::optional<assets::AssetId> chosen;
    const float buttons = ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginChild("##scenes", ImVec2{ 0.0f, -buttons }, ImGuiChildFlags_Borders))
    {
        for (const auto* entry : scenes)
        {
            const bool current = m_openSelection == entry->id;
            if (ImGui::Selectable(entry->path.c_str(), current, ImGuiSelectableFlags_AllowDoubleClick))
            {
                m_openSelection = entry->id;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    chosen = entry->id;
            }
            if (current && (moved || ImGui::IsWindowAppearing()))
                ImGui::SetScrollHereY();
        }
        if (scenes.empty())
            ImGui::TextDisabled("%s", m_openSearch.empty() ? "The project has no scene files" : "No scene matches the search");
    }
    ImGui::EndChild();

    // Open is the default: Enter takes it, and it has the accent fill
    ImGui::BeginDisabled(!m_openSelection);
    ImGui::PushStyleColor(ImGuiCol_Button, theme::accentFill);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::accentFillHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::accentFillActive);
    const bool open = ImGui::Button("Open") || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();
    if (open && m_openSelection)
        chosen = m_openSelection;
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    if (chosen)
    {
        ImGui::CloseCurrentPopup();
        request({ .kind = PendingAction::Kind::Open, .scene = *chosen }, frame);
    }
    ImGui::EndPopup();
}

void EditorShell::drawSaveAsPopup(FrameContext& frame)
{
    if (std::exchange(m_openSaveAs, false))
    {
        const auto* entry = m_scene ? frame.assets.database().find(*m_scene) : nullptr;
        m_saveAsPath = entry ? entry->path : "scenes/untitled.scene";
        m_fileError.clear();
        ImGui::OpenPopup("Save Scene As");
    }
    if (!ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextUnformatted("A path in the project's content folder:");
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(360.0f * theme::scale());
    const bool entered = ImGui::InputText("##path", &m_saveAsPath, ImGuiInputTextFlags_EnterReturnsTrue);
    if (!m_fileError.empty())
        ImGui::TextColored(theme::error, "%s", m_fileError.c_str());
    bool done = false;
    if (ImGui::Button("Save") || entered)
    {
        if (auto saved = saveAs(frame, m_saveAsPath); saved)
            done = true;
        else
            m_fileError = std::move(saved.error());
    }
    ImGui::SameLine();
    const bool cancelled = ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape);
    if (done || cancelled)
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    // A prompt's Save that went through Save As: its action runs once saved, and is dropped when cancelled.
    if (m_pending && !m_openPrompt && !ImGui::IsPopupOpen("Unsaved Changes"))
    {
        if (done)
            run(*std::exchange(m_pending, std::nullopt), frame);
        else if (cancelled)
            m_pending.reset();
    }
}

void EditorShell::drawUnsavedPrompt(FrameContext& frame)
{
    if (std::exchange(m_openPrompt, false))
        ImGui::OpenPopup("Unsaved Changes");
    if (!ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextUnformatted("The scene has unsaved changes. Save them first?");
    ImGui::Spacing();
    if (ImGui::Button("Save"))
    {
        ImGui::CloseCurrentPopup();
        if (save(frame))
            run(*std::exchange(m_pending, std::nullopt), frame);
        else if (!m_openSaveAs)
            m_pending.reset(); // The save failed (logged): nothing is discarded
    }
    ImGui::SameLine();
    if (ImGui::Button("Don't Save"))
    {
        ImGui::CloseCurrentPopup();
        deleteAutosave(m_scene);
        run(*std::exchange(m_pending, std::nullopt), frame);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        ImGui::CloseCurrentPopup();
        m_pending.reset();
    }
    ImGui::EndPopup();
}

void EditorShell::updateWorlds(FrameContext& frame)
{
    if (!m_playWorld)
    {
        frame.update(frame.world, world::UpdateMode::Edit); // Moved entities update, nothing plays
        return;
    }
    // Paused, edits still show (as in the edit world), but nothing plays.
    frame.update(*m_playWorld, m_play.shouldUpdate() ? world::UpdateMode::Play : world::UpdateMode::Edit);
    frame.render(*m_playWorld);
}

void EditorShell::startPlay(FrameContext& frame)
{
    if (m_playWorld)
        return;
    // The game's code runs in this process from here on: a crash must not take the unsaved changes with it
    if (m_context.unsaved())
        autosave(frame);
    auto& play = frame.engine.createWorld();
    const auto prepare = [&frame](const reflect::TypeInfo& type, void* component) { frame.assets.resolve(type, component, frame.types); };
    if (auto copied = copyWorld(frame.world.registry(), play.registry(), frame.types, prepare); !copied)
    {
        log::warn("Cannot play: {}", copied.error());
        frame.engine.destroyWorld(play);
        return;
    }
    m_viewport.endGizmoDrag(m_context, frame); // An edit still being dragged ends here
    m_context.undoStack().seal();
    m_context.enterPlay(play);
    m_playWorld = &play;
    m_play.play();
    m_viewport.setGameView(true);
    m_fullViewport.play(m_context.preferences().fullViewportOnPlay);
    syncFullViewport();
}

void EditorShell::stopPlay(FrameContext& frame)
{
    if (!m_playWorld)
        return;
    m_context.exitPlay();
    frame.engine.destroyWorld(*m_playWorld);
    m_playWorld = nullptr;
    m_play.stop();
    m_viewport.setGameView(false);
    frame.engine.restoreCursor(); // The game may have hidden it
    m_fullViewport.stop();
    syncFullViewport();
}

void EditorShell::selectByName(std::string_view name)
{
    auto& registry = m_context.world()->registry();
    // The view runs from the newest entity to the oldest, so its last match is the first one created
    std::optional<StableId> found;
    for (const auto handle : registry.view<StableId>())
    {
        const world::Entity entity{ handle };
        if (registry.has<Name>(entity) && registry.get<Name>(entity).name == name)
            found = registry.get<StableId>(entity);
    }
    if (found)
        m_context.selection().select(*found);
    else
        log::warn("--editor-select: no entity is called '{}'", name);
}

void EditorShell::savePreferencesIfChanged()
{
    auto& preferences = m_context.preferences();
    if (m_preferencesFile.empty() || preferences == m_savedPreferences)
        return;
    preferences.clamp();
    m_savedPreferences = preferences; // Also after a failure: it is reported once, not every frame
    if (const auto saved = savePreferences(preferences, m_preferencesFile); !saved)
        log::warn("The editor preferences are not saved: {}", saved.error());
}

auto EditorShell::autosaveFile(std::optional<assets::AssetId> scene) const -> std::filesystem::path
{
    if (m_autosaveFolder.empty())
        return {};
    return autosavePath(m_autosaveFolder, scene ? std::optional{ scene->value } : std::nullopt);
}

void EditorShell::updateAutosave(FrameContext& frame)
{
    if (m_autosaveFolder.empty() || !m_context.unsaved())
    {
        m_autosaveFrom.reset();
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!m_autosaveFrom)
        m_autosaveFrom = now;
    const int minutes = m_context.preferences().autosaveMinutes;
    if (minutes > 0 && now - *m_autosaveFrom >= std::chrono::minutes{ minutes })
        autosave(frame);
}

void EditorShell::autosave(FrameContext& frame)
{
    m_autosaveFrom = std::chrono::steady_clock::now(); // Also after a failure: it is retried at the next interval
    if (m_autosaveFolder.empty() || m_context.preferences().autosaveMinutes == 0)
        return;
    const auto file = autosaveFile(m_scene);
    if (auto written = writeAutosave(m_context.editWorld()->registry(), frame.types, m_scene ? m_scene->value : 0, file); !written)
        log::warn("The scene is not autosaved: {}", written.error());
}

void EditorShell::deleteAutosave(std::optional<assets::AssetId> scene)
{
    if (m_autosaveFolder.empty())
        return;
    const auto file = autosaveFile(scene);
    std::error_code error;
    std::filesystem::remove(file, error);
    if (error)
        log::warn("Cannot delete the autosave '{}': {}", file.string(), error.message());
    if (m_recovery && m_recovery->file == file)
        m_recovery.reset();
}

void EditorShell::offerRecovery(const assets::AssetDatabase& database)
{
    m_recovery.reset();
    if (m_autosaveFolder.empty())
        return;
    const auto file = autosaveFile(m_scene);
    const auto* entry = m_scene ? database.find(*m_scene) : nullptr;
    const auto written = findAutosave(file, entry ? database.absolutePath(*entry) : std::filesystem::path{});
    if (!written)
        return;
    const auto* zone = std::chrono::current_zone();
    const auto local = zone->to_local(std::chrono::clock_cast<std::chrono::system_clock>(*written));
    const auto today = std::chrono::floor<std::chrono::days>(zone->to_local(std::chrono::system_clock::now()));
    m_recovery = Recovery{ .file = file, .label = recoveryLabel(sceneFileName(), std::chrono::floor<std::chrono::seconds>(local), today) };
}

void EditorShell::drawRecoveryBar()
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::warning, "%s", theme::icons::recovery);
    ImGui::SameLine();
    ImGui::TextUnformatted(m_recovery->label.c_str());
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, theme::accentFill);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::accentFillHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::accentFillActive);
    if (ImGui::Button("Restore"))
        m_recoveryChoice = RecoveryChoice::Restore;
    ImGui::PopStyleColor(3);
    ImGui::SetItemTooltip("Open the autosaved changes as the scene, unsaved");
    ImGui::SameLine();
    if (ImGui::Button("Discard"))
        m_recoveryChoice = RecoveryChoice::Discard;
    ImGui::SetItemTooltip("Delete the autosave");
}

void EditorShell::restoreAutosave(FrameContext& frame)
{
    if (!m_recovery)
        return;
    const auto file = std::exchange(m_recovery, std::nullopt)->file;
    const auto text = readAutosave(file);
    if (!text)
    {
        log::warn("Cannot restore the autosave: {}", text.error());
        return;
    }
    const auto scene = serialize::parseScene(*text, frame.types);
    if (!scene)
    {
        log::warn("Cannot restore the autosave '{}': {}", file.string(), serialize::describe(scene.error()));
        return;
    }
    stopPlay(frame);
    const auto opened = m_scene; // The scene keeps its file and ID: Save writes the restored changes there
    frame.engine.newScene();
    const auto prepare = [&frame](const reflect::TypeInfo& type, void* component) { frame.assets.resolve(type, component, frame.types); };
    if (auto created = serialize::instantiate(frame.world.registry(), frame.types, *scene, prepare); !created)
    {
        // Nothing was created: the scene as its file has it again, and the bar with it
        log::warn("Cannot restore the autosave '{}': {}", file.string(), serialize::describe(created.error()));
        run(opened ? PendingAction{ .kind = PendingAction::Kind::Open, .scene = *opened } : PendingAction{ .kind = PendingAction::Kind::New }, frame);
        return;
    }
    m_scene = opened;
    m_context.resetScene();
    m_context.undoStack().markUnsaved(); // The saved state is the file, which is not in the history
}

void EditorShell::drawStatusBar(FrameContext& frame)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float height = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos(ImVec2{ viewport->Pos.x, viewport->Pos.y + viewport->Size.y - height });
    ImGui::SetNextWindowSize(ImVec2{ viewport->Size.x, height });
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 8.0f * theme::scale(), 2.0f * theme::scale() });
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::bgDeep);
    const bool visible = ImGui::Begin("##StatusBar", nullptr, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (visible)
    {
        drawDrawerButtons();
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
        ImGui::TextUnformatted(selectionLabel().c_str());
        ImGui::SameLine();
        const float left = ImGui::GetCursorPosX();

        const double frameMs = Profiler::instance().time("Frame Time"); // Its rolling average
        double gpuMs = 0.0;
        for (const auto& timing : frame.frameGraph.timings())
            gpuMs += timing.gpuMs;
        m_gpuMs.add(gpuMs);
        const auto timing = std::format("{:.1f} fps ({:.2f} ms)  |  GPU {:.2f} ms", frameMs > 0.0 ? 1000.0 / frameMs : 0.0, frameMs, m_gpuMs.average());
        const float right = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - ImGui::CalcTextSize(timing.c_str()).x;

        drawKeyHints(keyHints(), left, right);
        ImGui::SameLine(right);
        ImGui::TextDisabled("%s", timing.c_str());
    }
    ImGui::End();
}

void EditorShell::drawDrawerButtons()
{
    // Small buttons, as tall as the bar's text; lit while their panel shows
    const auto button = [this](const char* icon, std::string_view panel, std::string_view action) {
        const bool lit = m_drawers.isShown(panel);
        if (lit)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, theme::accentFill);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::accentFillHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::accentFillActive);
        }
        const std::string id{ panel };
        ImGui::PushID(id.c_str());
        if (ImGui::SmallButton(icon))
            toggleDrawer(panel);
        ImGui::PopID();
        if (lit)
            ImGui::PopStyleColor(3);
        ImGui::SetItemTooltip("%s", m_actions.tooltip(action).c_str());
    };
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });
    button(theme::icons::assets, "Assets", "view.assets");
    ImGui::SameLine();
    button(theme::icons::console, "Console", "view.console");
    ImGui::SameLine(0.0f, 0.0f); // The counts are part of the Console button
    drawProblemCounts();
    ImGui::PopStyleColor();
}

auto EditorShell::keyHints() const -> std::vector<KeyHint>
{
    // The viewport's own keys are no actions (they depend on its focus); the others come from the registry
    if (m_viewport.flying())
        return { { "W A S D", "move" }, { "Q E", "down, up" }, { "Wheel", "speed" } };
    if (m_viewport.hovered())
    {
        if (m_viewport.gameView())
            return { { "Click", "give the game the input" }, { m_actions.shortcut("play.release"), "take it back" } };
        return { { "Q W E R", "select, move, rotate, scale" }, { m_actions.shortcut("edit.focus"), "focus" }, { "RMB", "fly" }, { "Alt+LMB", "orbit" }, { "MMB", "pan" } };
    }
    const ImGuiWindow* hovered = ImGui::GetCurrentContext()->HoveredWindow;
    if (hovered && std::string_view{ hovered->RootWindow->Name } == "Hierarchy")
        return { { m_actions.shortcut("edit.rename"), "rename" }, { "Drag", "reparent" }, { m_actions.shortcut("edit.duplicate"), "duplicate" }, { m_actions.shortcut("edit.delete"), "delete" } };
    return {};
}

void EditorShell::drawKeyHints(const std::vector<KeyHint>& hints, float left, float right)
{
    if (hints.empty())
        return;
    // Each key in the text color and what it does muted, the pairs apart by a wide gap
    const ImGuiStyle& style = ImGui::GetStyle();
    const float gap = style.ItemSpacing.x * 3.0f;
    float width = 0.0f;
    for (const auto& hint : hints)
        width += ImGui::CalcTextSize(hint.keys.c_str()).x + style.ItemSpacing.x + ImGui::CalcTextSize(hint.does.data(), hint.does.data() + hint.does.size()).x + gap;
    width -= gap;
    const float space = right - left - 2.0f * gap;
    if (width > space)
        return; // No room between the selection and the timing
    ImGui::SameLine(left + gap + (space - width) * 0.5f);
    for (const auto& hint : hints)
    {
        if (&hint != &hints.front())
            ImGui::SameLine(0.0f, gap);
        ImGui::TextUnformatted(hint.keys.c_str());
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::textMuted);
        ImGui::TextUnformatted(hint.does.data(), hint.does.data() + hint.does.size());
        ImGui::PopStyleColor();
    }
}

auto EditorShell::selectionLabel() const -> std::string
{
    const auto entity = m_context.selectedEntity();
    if (!entity)
        return "No selection";
    auto& registry = m_context.world()->registry();
    std::string label = registry.has<Name>(entity) ? registry.get<Name>(entity).name : "Entity";
    if (const auto count = m_context.selection().ids().size(); count > 1)
        label += std::format(" and {} more", count - 1);
    return label;
}

void EditorShell::drawProblemCounts()
{
    const auto counts = m_context.console().counts();
    const auto warnings = std::format("{} {}", theme::icons::warning, counts.warnings);
    const auto errors = std::format("{} {}", theme::icons::error, counts.errors);

    const auto problem = m_context.console().lastProblem();
    const auto tooltip = problem ? std::format("{}: {}\nClick to show the warnings and errors in the console", log::levelName(problem->level), problem->message)
                                 : std::string{ "No warnings or errors" };
    // A count of zero is muted: only the levels that have messages draw the eye
    const auto count = [&](const std::string& label, std::size_t value, const ImVec4& color) {
        ImGui::PushStyleColor(ImGuiCol_Text, value > 0 ? color : theme::textMuted);
        const bool clicked = ImGui::SmallButton(label.c_str());
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s", tooltip.c_str());
        if (clicked)
        {
            showDrawer("Console");
            m_context.requestConsoleProblems();
        }
    };
    count(warnings, counts.warnings, theme::warning);
    ImGui::SameLine(0.0f, 0.0f);
    count(errors, counts.errors, theme::error);
}

void EditorShell::buildActions()
{
    const auto chord = [](Key key, bool ctrl = true, bool shift = false, bool alt = false) { return Chord{ .key = key, .ctrl = ctrl, .shift = shift, .alt = alt }; };
    const auto hasSelection = [this] { return static_cast<bool>(m_context.selectedEntity()); };

    m_actions.add({ .id = "file.new", .label = "New", .chord = chord(Key::N), .run = [this](FrameContext& frame) { request({ .kind = PendingAction::Kind::New }, frame); } });
    m_actions.add({ .id = "file.open", .label = "Open...", .chord = chord(Key::O), .run = [this](FrameContext&) { m_openOpen = true; } });
    m_actions.add({ .id = "file.save", .label = "Save", .chord = chord(Key::S), .run = [this](FrameContext& frame) { save(frame); } });
    m_actions.add({ .id = "file.saveAs", .label = "Save As...", .chord = chord(Key::S, true, true), .run = [this](FrameContext&) { m_openSaveAs = true; } });

    m_actions.add({ .id = "edit.undo",
        .label = "Undo",
        .chord = chord(Key::Z),
        .repeats = true,
        .enabled = [this] { return m_context.undoStack().canUndo(); },
        .run = [this](FrameContext& frame) { undoEdit(m_context, frame); } });
    m_actions.add({ .id = "edit.redo",
        .label = "Redo",
        .chord = chord(Key::Y),
        .alternate = chord(Key::Z, true, true),
        .repeats = true,
        .enabled = [this] { return m_context.undoStack().canRedo(); },
        .run = [this](FrameContext& frame) { redoEdit(m_context, frame); } });
    m_actions.add({ .id = "edit.duplicate", .label = "Duplicate", .chord = chord(Key::D), .enabled = hasSelection, .run = [this](FrameContext& frame) { duplicateSelected(m_context, frame); } });
    m_actions.add({ .id = "edit.delete", .label = "Delete", .chord = chord(Key::Delete, false), .enabled = hasSelection, .run = [this](FrameContext& frame) { destroySelected(m_context, frame); } });
    m_actions.add({ .id = "edit.preferences", .label = "Preferences...", .run = [this](FrameContext&) { m_preferencesWindow->setOpen(true); } });
    m_actions.add({ .id = "edit.rename",
        .label = "Rename",
        .chord = chord(Key::F2, false),
        .enabled = hasSelection,
        .run = [this](FrameContext&) { m_context.requestRename(); } });
    m_actions.add({ .id = "edit.focus", .label = "Focus", .chord = chord(Key::F, false), .enabled = hasSelection, .run = [this](FrameContext&) { m_context.requestFocus(); } });

    m_actions.add({ .id = "view.assets", .label = "Assets", .chord = chord(Key::Space), .run = [this](FrameContext&) { toggleDrawer("Assets"); } });
    m_actions.add({ .id = "view.console", .label = "Console", .chord = chord(Key::Space, true, true), .run = [this](FrameContext&) { toggleDrawer("Console"); } });
    m_actions.add({ .id = "view.fullViewport", .label = "Full Viewport", .chord = chord(Key::F11, false), .run = [this](FrameContext&) { toggleFullViewport(); } });

    m_actions.add({ .id = "help.shortcuts", .label = "Keyboard Shortcuts", .run = [this](FrameContext&) { m_shortcutsWindow->setOpen(true); } });
    m_actions.add({ .id = "help.about", .label = "About", .run = [this](FrameContext&) { m_aboutWindow->setOpen(true); } });

    m_actions.add({ .id = "play.toggle", .label = "Play", .chord = chord(Key::P), .run = [this](FrameContext& frame) { m_play.active() ? stopPlay(frame) : startPlay(frame); } });
    m_actions.add({ .id = "play.pause",
        .label = "Pause",
        .chord = chord(Key::P, true, true),
        .enabled = [this] { return m_play.active(); },
        .run = [this](FrameContext&) { m_play.state() == PlaySession::State::Paused ? m_play.play() : m_play.pause(); } });
    m_actions.add({ .id = "play.step",
        .label = "Step",
        .chord = chord(Key::P, true, false, true),
        .enabled = [this] { return m_play.state() == PlaySession::State::Paused; },
        .run = [this](FrameContext&) { m_play.step(); } });
    m_actions.add({ .id = "play.release",
        .label = "Release Input",
        .chord = chord(Key::F1, false, true),
        .enabled = [this] { return m_play.active(); },
        .run = [this](FrameContext& frame) {
            m_viewport.releaseInput();
            frame.engine.restoreCursor(); } });
}

void EditorShell::updateStyle()
{
    const float contentScale = m_engine->contentScale();
    const float uiScale = m_context.preferences().uiScale;
    if (contentScale == m_contentScale && uiScale == m_uiScale)
        return;
    m_contentScale = contentScale;
    m_uiScale = uiScale;
    theme::applyStyle(contentScale, uiScale);
}

void EditorShell::drawDockspace()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float bar = ImGui::GetFrameHeight(); // The menu bar above and the status bar below
    const ImVec2 size{ viewport->Size.x, std::max(viewport->Size.y - 2.0f * bar, 1.0f) };
    ImGui::SetCursorPos(ImVec2{ 0.0f, bar });
    const ImGuiID id = ImGui::GetID("EditorDockSpace");
    m_dockspace = id;
    if (std::exchange(m_resetLayout, false) || !ImGui::DockBuilderGetNode(id))
        buildDefaultLayout(id, size);
    dockPinned(); // Before the dockspace lays out its nodes, so a pinned panel shows docked at once
    // The panels' tabs have their own close buttons, and the View menu lists them: no window menu or close-all per node
    ImGui::DockSpace(id, size, ImGuiDockNodeFlags_NoWindowMenuButton | ImGuiDockNodeFlags_NoCloseButton);
}

auto EditorShell::findPanel(std::string_view name) const -> Panel*
{
    const auto it = std::ranges::find(m_panels, name, [](const auto& panel) { return panel->name(); });
    return it == m_panels.end() ? nullptr : it->get();
}

void EditorShell::openPanel(std::string_view name)
{
    if (auto* panel = findPanel(name))
        panel->setOpen(true);
}

void EditorShell::toggleFullViewport()
{
    m_fullViewport.toggle();
    syncFullViewport();
}

void EditorShell::syncFullViewport()
{
    const bool full = m_fullViewport.active();
    if (full == m_viewport.fullWindow())
        return;
    if (full)
        m_drawers.close(); // Esc would have to close it before it leaves full viewport
    m_viewport.setFullWindow(full);
}

void EditorShell::leaveFullViewportOnEscape(bool drawerWasShown)
{
    // The same Esc as the drawer's: the first one closes the drawer, a popup takes its own
    if (!m_fullViewport.active() || drawerWasShown || !ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        return;
    if (ImGui::GetIO().WantTextInput || ImGui::GetDragDropPayload() || m_viewport.givesInputToGame() || m_viewport.cameraDragged() || ImGuizmo::IsUsing() ||
        ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        return;
    m_fullViewport.leave();
    syncFullViewport();
}

void EditorShell::drawFullViewportBar(FrameContext& frame)
{
    drawPlayButtons(frame);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
    // The way back stays in sight: the button, and its shortcut next to it
    const auto shortcut = m_actions.shortcut("view.fullViewport");
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });
    const bool leave = ImGui::SmallButton(theme::icons::leaveFullViewport);
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("%s", std::format("Leave full viewport ({}, Esc)", shortcut).c_str());
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextDisabled("%s", shortcut.c_str());
    if (leave)
        runAction("view.fullViewport", frame);
}

void EditorShell::showDrawer(std::string_view name)
{
    openPanel(name);
    if (m_drawers.show(name))
    {
        m_selectDrawerTab = true;
        m_focusDrawer = true;
    }
    else
        m_focusPinned = name; // Its tab shows
}

void EditorShell::toggleDrawer(std::string_view name)
{
    openPanel(name);
    if (!m_drawers.toggle(name))
        m_focusPinned = name; // Pinned: its tab shows
    else if (m_drawers.isShown(name))
    {
        m_selectDrawerTab = true;
        m_focusDrawer = true;
    }
}

void EditorShell::pin(std::string_view name)
{
    m_drawers.pin(name);
    if (std::ranges::find(m_dockRequests, name) == m_dockRequests.end())
        m_dockRequests.emplace_back(name);
    m_focusPinned = name;
    ImGui::MarkIniSettingsDirty();
}

void EditorShell::unpin(std::string_view name)
{
    m_drawers.unpin(name);
    m_selectDrawerTab = true;
    m_focusDrawer = true;
    ImGui::MarkIniSettingsDirty();
}

auto EditorShell::pinNode(std::string_view name) -> std::uint32_t
{
    // A leaf of the layout's tree that is not the viewport's; one that lost its windows stays, hidden, until reused
    const auto usable = [this](ImGuiID id) {
        ImGuiDockNode* node = id != 0 ? ImGui::DockBuilderGetNode(id) : nullptr;
        return node && node->IsLeafNode() && !node->IsCentralNode() && ImGui::DockNodeGetRootNode(node)->ID == m_dockspace;
    };
    if (const ImGuiID own = dockIdOf(name); usable(own))
        return own;
    for (const auto& other : m_drawers.pinned())
    {
        if (const ImGuiID id = dockIdOf(other); other != name && usable(id))
            return id;
    }
    if (usable(m_pinNode))
        return m_pinNode;
    ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(m_dockspace);
    if (!central)
        return 0;
    ImGuiID rest = 0;
    return ImGui::DockBuilderSplitNode(central->ID, ImGuiDir_Down, pinnedShare, nullptr, &rest); // The viewport stays central
}

void EditorShell::dockPinned()
{
    if (m_dockRequests.empty())
        return;
    for (const auto& name : std::exchange(m_dockRequests, {}))
    {
        if (!m_drawers.isPinned(name))
            continue;
        if (const ImGuiID node = pinNode(name); node != 0)
        {
            ImGui::DockBuilderDockWindow(name.c_str(), node);
            m_pinNode = node;
        }
    }
    ImGui::DockBuilderFinish(m_dockspace);
}

void EditorShell::drawPinned(Panel& panel, FrameContext& frame)
{
    const std::string name{ panel.name() };
    if (m_focusPinned == name)
    {
        ImGui::SetNextWindowFocus(); // Also selects its tab
        m_focusPinned.clear();
    }
    bool open = true;
    const bool visible = ImGui::Begin(name.c_str(), panel.debug() ? &open : nullptr); // Assets and the console stay open
    if (!open)
        panel.setOpen(false);
    // Appearing undocked (pinned in the layout file without a node, or dragged out to float before): docked below the
    // viewport before the next frame's dockspace. Only as it appears: a drag to another node undocks it meanwhile.
    if (ImGui::IsWindowAppearing() && !ImGui::IsWindowDocked() && std::ranges::find(m_dockRequests, name) == m_dockRequests.end())
        m_dockRequests.push_back(name);
    if (visible)
    {
        const float button = ImGui::GetFrameHeight();
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float right = start.x + ImGui::GetContentRegionAvail().x;
        panel.setTrailingWidth(button + ImGui::GetStyle().ItemSpacing.x);
        panel.draw(m_context, frame);
        panel.setTrailingWidth(0.0f);
        // Over the right end of the panel's first row, which leaves it free
        ImGui::SetCursorScreenPos(ImVec2{ right - button, start.y });
        if (theme::iconButton(theme::icons::pin, "##unpin", true))
            unpin(name);
        ImGui::SetItemTooltip("Unpin: back to the drawer");
    }
    ImGui::End();
}

void EditorShell::drawColumnPanel(Panel& panel, FrameContext& frame)
{
    const std::string title{ panel.title() };
    if (ImGui::Begin(title.c_str()))
        panel.draw(m_context, frame);
    ImGui::End();
}

void EditorShell::drawDrawer(FrameContext& frame)
{
    const ImGuiIO& io = ImGui::GetIO();
    // A drag that began in the drawer closes it once it leaves it. ImGui keeps the payload while the button is held,
    // though its source is no longer drawn, so the drop still lands in the viewport or on an inspector slot.
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (!payload)
        m_dragFromDrawer = m_dragHidDrawer = false;
    else if (!m_dragHidDrawer && contains(m_drawerRect, io.MouseClickedPos[ImGuiMouseButton_Left]))
        m_dragFromDrawer = true;
    if (m_dragFromDrawer && m_drawers.shown() && !contains(m_drawerRect, io.MousePos))
    {
        m_drawers.close();
        m_dragFromDrawer = false;
        m_dragHidDrawer = true;
    }

    // Esc and a click into the viewport close it; losing the focus doesn't (popups, the asset picker and drags take it)
    if (m_drawers.shown())
    {
        const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !io.WantTextInput && !payload && !m_viewport.givesInputToGame() &&
                            !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        const bool click = m_viewport.hovered() &&
                           (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle));
        if (escape || click)
            m_drawers.close();
    }

    const auto shown = m_drawers.shown();
    const Panel* panel = shown ? findPanel(*shown) : nullptr;
    if (shown && (!panel || !panel->isOpen()))
        m_drawers.close();
    const auto viewport = m_viewport.imageRect();
    if (!m_drawers.shown() || !viewport)
    {
        m_drawerRect.reset();
        m_focusDrawer = false;
        return;
    }

    // Over the bottom of the image and as wide: the render extent stays, and the right column stays usable
    const float height = std::clamp(std::round(viewport->height * m_context.preferences().drawerHeight), std::min(4.0f * ImGui::GetFrameHeight(), viewport->height), viewport->height);
    const Viewport::Rect rect{ .x = viewport->x, .y = viewport->y + viewport->height - height, .width = viewport->width, .height = height };
    m_drawerRect = rect;
    ImGui::SetNextWindowPos(ImVec2{ rect.x, rect.y });
    ImGui::SetNextWindowSize(ImVec2{ rect.width, rect.height });
    if (std::exchange(m_focusDrawer, false))
        ImGui::SetNextWindowFocus();
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin("##Drawer", nullptr, flags))
    {
        drawDrawerResize(*viewport, rect.y);

        // A tab per open drawer panel that is not pinned (the debug panels' close), and the pin and close buttons at the
        // right end of the row. ImGui's trailing tabs follow the last tab instead, so the bar is ImGui::BeginTabBar's
        // made narrower: the tabs stop short of the buttons, and its separator still spans the row.
        const ImGuiStyle& style = ImGui::GetStyle();
        const float side = ImGui::GetFrameHeight(); // The bar's height
        const ImVec2 row = ImGui::GetCursorScreenPos();
        const float right = row.x + ImGui::GetContentRegionAvail().x;
        const float buttons = 2.0f * side + style.ItemInnerSpacing.x;
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const ImGuiID barId = window->GetID("##tabs");
        ImGuiTabBar* bar = ImGui::GetCurrentContext()->TabBars.GetOrAddByKey(barId);
        bar->ID = barId;
        bar->SeparatorMinX = row.x - std::trunc(window->WindowPadding.x * 0.5f);
        bar->SeparatorMaxX = right + std::trunc(window->WindowPadding.x * 0.5f);
        const ImRect bounds{ row.x, row.y, std::max(row.x, right - buttons - style.ItemSpacing.x), row.y + side };

        const Panel* selected = nullptr;
        bool pinShown = false;
        bool close = false;
        if (ImGui::BeginTabBarEx(bar, bounds, ImGuiTabBarFlags_IsFocused))
        {
            for (const auto& candidate : m_panels)
            {
                const std::string name{ candidate->name() };
                if (!candidate->drawer() || !candidate->isOpen() || m_drawers.isPinned(name))
                    continue;
                const bool select = m_selectDrawerTab && m_drawers.isShown(name);
                bool open = true;
                if (ImGui::BeginTabItem(name.c_str(), candidate->debug() ? &open : nullptr, select ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None))
                {
                    selected = candidate.get();
                    ImGui::EndTabItem();
                }
                if (!open)
                {
                    candidate->setOpen(false);
                    m_drawers.hide(name);
                }
            }
            ImGui::EndTabBar();
        }
        const ImVec2 below = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2{ right - buttons, row.y });
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });
        pinShown = theme::iconButton(theme::icons::pin, "##pin");
        ImGui::SetItemTooltip("Pin: dock it below the viewport");
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        close = theme::iconButton(theme::icons::close, "##close");
        ImGui::SetItemTooltip("Close (Esc)");
        ImGui::PopStyleColor();
        // A tab the user clicked shows; one this shell selected may take a frame to be
        if (selected && m_drawers.isShown(selected->name()))
            m_selectDrawerTab = false;
        else if (selected && !m_selectDrawerTab)
            m_drawers.show(selected->name());

        if (const auto now = m_drawers.shown(); pinShown && now)
            pin(std::string{ *now }); // A copy: pinning closes the drawer, and with it the name it shows
        else if (close)
            m_drawers.close();
        else if (now)
        {
            if (auto* contents = findPanel(*now); contents && contents->isOpen())
            {
                // Back below the tabs, only with an item to follow: ImGui asserts on a cursor moved past the last item
                ImGui::SetCursorScreenPos(below);
                const std::string id{ *now };
                ImGui::PushID(id.c_str());
                if (ImGui::BeginChild("##contents"))
                    contents->draw(m_context, frame);
                ImGui::EndChild();
                ImGui::PopID();
            }
        }
    }
    ImGui::End();
}

void EditorShell::drawDrawerResize(const Viewport::Rect& viewport, float top)
{
    // A strip along the top edge, inside the window's padding, where ImGui would clip it
    const float grip = 4.0f * theme::scale();
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 position = ImGui::GetWindowPos();
    const float width = ImGui::GetWindowWidth();
    ImGui::PushClipRect(position, ImVec2{ position.x + width, position.y + grip }, false);
    ImGui::SetCursorScreenPos(ImVec2{ position.x, top });
    ImGui::InvisibleButton("##resize", ImVec2{ width, grip });
    // The edge is the border's line, the accent's while it can be dragged
    const bool active = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();
    if (active || hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    const ImVec4 color = active ? theme::accentActive : (hovered ? theme::accent : theme::border);
    ImGui::GetWindowDrawList()->AddLine(ImVec2{ position.x, top + 1.0f }, ImVec2{ position.x + width, top + 1.0f }, theme::packed(color), 2.0f * theme::scale());
    if (active)
    {
        const float share = (viewport.y + viewport.height - ImGui::GetIO().MousePos.y) / viewport.height;
        m_context.preferences().drawerHeight = std::clamp(share, minDrawerHeight, maxDrawerHeight);
    }
    ImGui::PopClipRect();
    ImGui::SetCursorScreenPos(cursor);
}

void EditorShell::drawDragTooltip(FrameContext& frame)
{
    // The asset's source went with the drawer, and its tooltip with it
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (!m_dragHidDrawer || !payload || !payload->IsDataType(assetPayloadType) || payload->DataSize != static_cast<int>(sizeof(AssetPayload)))
        return;
    AssetPayload asset;
    std::memcpy(&asset, payload->Data, sizeof(asset));
    const auto* entry = frame.assets.database().find(assets::AssetId{ asset.id });
    if (!entry)
        return;
    const auto name = entry->key.empty() ? std::filesystem::path{ entry->path }.filename().string() : entry->key;
    ImGui::SetTooltip("%s", std::format("{} ({})", name, asset.assetType()).c_str());
}

void EditorShell::resetLayout()
{
    m_resetLayout = true;
    for (auto* panel : m_defaultOpen)
        panel->setOpen(true);
    m_drawers.reset();
    m_dockRequests.clear();
    m_pinNode = 0;
    ImGui::MarkIniSettingsDirty();
}

void EditorShell::drawStyleEditor()
{
    if (!m_styleEditor)
        return;
    if (ImGui::Begin("Style Editor", &m_styleEditor))
        ImGui::ShowStyleEditor();
    ImGui::End();
}

void EditorShell::drawMenuBar(FrameContext& frame)
{
    if (!ImGui::BeginMainMenuBar())
        return;

    drawFileMenu(frame);

    if (ImGui::BeginMenu("Edit"))
    {
        m_actions.menuItem("edit.undo", frame, editLabel("Undo", m_context.undoStack().nextUndo(), m_context.undoStack().nextUndoTitle()));
        m_actions.menuItem("edit.redo", frame, editLabel("Redo", m_context.undoStack().nextRedo(), m_context.undoStack().nextRedoTitle()));
        ImGui::Separator();
        m_actions.menuItem("edit.duplicate", frame);
        m_actions.menuItem("edit.delete", frame);
        m_actions.menuItem("edit.rename", frame);
        ImGui::Separator();
        m_actions.menuItem("edit.preferences", frame);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Entity"))
    {
        drawCreateMenu(m_context, frame);
        ImGui::Separator();
        m_actions.menuItem("edit.focus", frame);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Play"))
    {
        const auto state = m_play.state();
        m_actions.menuItem("play.toggle", frame, state == PlaySession::State::Stopped ? "Play" : "Stop");
        m_actions.menuItem("play.pause", frame, state == PlaySession::State::Paused ? "Resume" : "Pause");
        m_actions.menuItem("play.step", frame);
        ImGui::Separator();
        m_actions.menuItem("play.release", frame);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        for (const auto& panel : m_panels)
        {
            if (panel->debug())
                continue;
            const std::string name{ panel->name() };
            if (panel->drawer())
            {
                // Opens the drawer (its button and chord also close it)
                const auto chord = m_actions.shortcut(name == "Assets" ? "view.assets" : "view.console");
                if (ImGui::MenuItem(name.c_str(), chord.c_str(), m_drawers.isShown(name) || m_drawers.isPinned(name)))
                    showDrawer(name);
                continue;
            }
            if (ImGui::MenuItem(name.c_str(), nullptr, panel->isOpen()))
                panel->setOpen(!panel->isOpen());
        }
        ImGui::Separator();
        m_actions.menuItem("view.fullViewport", frame, {}, m_fullViewport.active());
        if (ImGui::MenuItem("Reset Layout"))
            resetLayout();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Debug"))
    {
        for (const auto& panel : m_panels)
        {
            if (!panel->debug())
                continue;
            const std::string name{ panel->name() };
            if (!ImGui::MenuItem(name.c_str(), nullptr, panel->isOpen()))
                continue;
            if (!panel->drawer())
                panel->setOpen(!panel->isOpen());
            else if (panel->isOpen())
            {
                panel->setOpen(false); // Its tab goes
                m_drawers.hide(name);
            }
            else
                showDrawer(name);
        }
        ImGui::MenuItem("Style Editor", nullptr, &m_styleEditor);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help"))
    {
        m_actions.menuItem("help.shortcuts", frame);
        ImGui::Separator();
        m_actions.menuItem("help.about", frame);
        ImGui::EndMenu();
    }

    drawPlayControls(frame);
    drawSceneName(frame);
    ImGui::EndMainMenuBar();
}

auto EditorShell::editLabel(std::string_view verb, std::span<const Edit> edits, std::string_view title) const -> std::string
{
    if (edits.empty())
        return std::string{ verb };
    return std::format("{} {}", verb, entryTitle(edits, title, m_context.world()->registry()));
}

void EditorShell::drawSceneName(FrameContext& frame)
{
    const auto name = sceneName();
    const auto& style = ImGui::GetStyle();
    const float right = ImGui::GetWindowWidth() - style.WindowPadding.x;
    const float button = ImGui::CalcTextSize(theme::icons::fullViewport).x + 2.0f * style.FramePadding.x;
    // The button at the right end, and the name left of it when there is room for both; the name goes first
    const float nameX = right - ImGui::CalcTextSize(name.c_str()).x;
    const bool showName = nameX - style.ItemSpacing.x - button > ImGui::GetCursorPosX();
    const float buttonX = showName ? nameX - style.ItemSpacing.x - button : right - button;
    if (buttonX <= ImGui::GetCursorPosX())
        return; // No room next to the menus and play controls
    ImGui::SetCursorPosX(buttonX);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });
    if (ImGui::SmallButton(theme::icons::fullViewport))
        runAction("view.fullViewport", frame);
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("%s", m_actions.tooltip("view.fullViewport").c_str());
    if (!showName)
        return;
    ImGui::SetCursorPosX(nameX);
    ImGui::TextUnformatted(name.c_str());
}

void EditorShell::runAction(std::string_view id, FrameContext& frame)
{
    if (const auto* action = m_actions.find(id); action && (!action->enabled || action->enabled()))
        action->run(frame);
}

void EditorShell::drawPlayControls(FrameContext& frame)
{
    const auto& style = ImGui::GetStyle();
    const float buttonWidth = ImGui::CalcTextSize(theme::icons::play).x + 2.0f * style.FramePadding.x;
    const float width = 3.0f * buttonWidth + 2.0f * style.ItemSpacing.x;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), (ImGui::GetWindowWidth() - width) * 0.5f));
    drawPlayButtons(frame);
}

void EditorShell::drawPlayButtons(FrameContext& frame)
{
    const auto state = m_play.state();

    // A small icon button that shows its action's name and shortcut; 'lit' colors the icon, 'filled' fills the button
    // with the play color instead.
    const auto button = [&](const char* icon, const char* id, std::string_view action, bool lit, const ImVec4& color, bool filled = false) {
        ImGui::PushID(id);
        int colors = 0;
        if (filled)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::bgDeep);
            ImGui::PushStyleColor(ImGuiCol_Button, theme::playFrame);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::playHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::playActive);
            colors = 4;
        }
        else if (lit)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            colors = 1;
        }
        const bool clicked = ImGui::SmallButton(icon);
        ImGui::PopStyleColor(colors);
        ImGui::PopID();
        ImGui::SetItemTooltip("%s", m_actions.tooltip(action).c_str());
        return clicked;
    };
    const auto run = [&](std::string_view id) { runAction(id, frame); };

    const bool active = state != PlaySession::State::Stopped;
    if (button(active ? theme::icons::stop : theme::icons::play, "play", "play.toggle", false, theme::text, active))
        run("play.toggle");
    ImGui::SameLine(); // A menu bar lays its items out in a row by itself; full viewport's top bar doesn't
    ImGui::BeginDisabled(!m_play.active());
    if (button(theme::icons::pause, "pause", "play.pause", state == PlaySession::State::Paused, theme::accentHover))
        run("play.pause");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(state != PlaySession::State::Paused);
    if (button(theme::icons::step, "step", "play.step", false, theme::text))
        run("play.step");
    ImGui::EndDisabled();
    if (active)
    {
        ImGui::SameLine();
        ImGui::TextColored(theme::playFrame, "%s", state == PlaySession::State::Paused ? "Paused" : "Playing");
    }
}
}
