module;

#include <cassert>
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
import :edits;
import :options;
import :panels;
import :statistics;
import :theme;
import :widgets;

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

    // The dockspace is wider than the window while the right column is folded: the window never scrolls to it
    ImGui::SetNextWindowScroll(ImVec2{ 0.0f, 0.0f });
    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                         ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                                         ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("Main Window", nullptr, windowFlags);
    ImGui::PopStyleVar(3);
}

// The menu bar's frame padding: taller than a frame's, so its buttons have room above and below them.
auto menuBarPadding() -> ImVec2
{
    return ImVec2{ ImGui::GetStyle().FramePadding.x, theme::menuBarPaddingY() };
}

// A slim vertical line at 'x' on the bar's text line starting at 'top', between groups of the status bar.
void drawBarLine(ImDrawList* drawList, float x, float top)
{
    const float fontSize = ImGui::GetFontSize();
    const float width = std::max(1.0f, std::round(theme::scale()));
    const float height = std::round(fontSize * 0.8f);
    const float lineTop = std::round(top + (fontSize - height) * 0.5f);
    drawList->AddRectFilled(ImVec2{ x, lineTop }, ImVec2{ x + width, lineTop + height }, theme::packed(theme::bgFrameActive));
}

// A timing of the status bar: a muted label, the number in tabular digits and its muted unit, in a slot as wide as
// 'widest' (a sample of the widest usual number) takes; a wider number widens it.
struct TimingSlot
{
    std::string_view label;
    std::string number;
    std::string_view unit;
    std::string_view widest;
};

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

}

EditorShell::EditorShell()
{
    // The console sees every message from here on, and the log still writes to stderr.
    m_logSink = log::addSink([this](log::Level level, std::string_view message) { m_context.console().push(level, message); });

    m_workspace.add(makeScenePanel(m_actions));
    m_workspace.add(makeInspectorPanel());
    m_workspace.add(makeContentPanel());
    m_workspace.add(makeConsolePanel());
    m_workspace.add(makeRendererPanel());
    m_workspace.add(makeStatisticsPanel());
    m_workspace.add(makeProfilerPanel());
    m_workspace.add(makeAllEntitiesPanel());
    m_workspace.add(makePreferencesPanel()); // The dialogs: Edit > Preferences... and the Help menu open them
    m_workspace.add(makeShortcutsPanel(m_actions));
    m_workspace.add(makeAboutPanel());
    buildActions();
    m_viewport.setReleaseHint(std::format("{} releases the input", m_actions.shortcut("play.release")));
}

EditorShell::~EditorShell()
{
    log::removeSink(m_logSink);
}

auto EditorShell::onStartup(Engine& engine) -> std::expected<void, EngineError>
{
    const auto options = parseOptions(engine.desc().shellOptions);
    if (!options)
        return std::unexpected{ options.error() };
    m_engine = &engine;
    theme::loadFonts(paths::content() / "fonts");
    // A drag in a window's body is the viewport's (orbit, gizmo), not a window move
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    if (options->allPanels)
        m_workspace.openAll();
    m_context.setWorld(engine.world()); // Now, so the first frame keeps a selection made here
    m_document.opened(m_startupScene ? std::optional{ m_startupScene->value } : std::nullopt);
    if (!options->select.empty())
        selectByName(options->select);
    m_playAfterFirstFrame = options->play;
    m_stopAfter = options->stopAfter;
    if (options->fullViewport)
        m_workspace.toggleFullViewport();
    m_foldOnFirstFrame = options->fold;
    if (options->checkStats)
        m_statisticsCheck = std::make_unique<StatisticsCheck>();
    // A headless run (a test, a screenshot) neither reads nor changes the user's layout.
    if (!engine.headless() && !engine.project().empty())
    {
        m_layoutFile = layoutFile(engine.project());
        m_recentFile = recentScenesPath(engine.project());
        m_document.setAutosaveFolder(autosaveFolder(engine.project()));
        if (auto loaded = loadRecentScenes(m_recentFile))
            m_recent = std::move(*loaded);
        else
            log::warn("The recent scenes are reset: {}", describe(loaded.error()));
    }
    m_workspace.addSettingsHandler(); // Before the layout file is read, at the first frame
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
            log::warn("The editor preferences are reset: {}", describe(loaded.error()));
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
    if (m_statisticsCheck)
        m_statisticsCheck->onShutdown();
    if (m_stopAfter && m_stops == 0)
        log::error("--editor-stop: play never stopped");
    savePreferencesIfChanged();
    // ImGui would save on destroying its context, after this shell (and the file name) are gone.
    if (!m_layoutFile.empty())
    {
        ImGui::SaveIniSettingsToDisk(m_layoutFile.c_str());
        ImGui::GetIO().IniFilename = nullptr;
    }
    m_workspace.removeSettingsHandler();
}

auto EditorShell::renderViews() const -> std::vector<RenderView>
{
    if (auto view = m_viewport.renderView())
        return { std::move(*view) };
    return {};
}

auto EditorShell::canClose() -> bool
{
    return m_document.canClose();
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
    if (m_document.update(std::chrono::steady_clock::now()))
        autosave(frame);

    drawUi(frame);
    updateWorlds(frame);
    if (m_statisticsCheck)
        m_statisticsCheck->onFrame(m_context, frame);
    // --editor-play: the first frame shows edit mode; the next one clicks Play
    if (std::exchange(m_playAfterFirstFrame, false))
        m_clickPlay = true;
    // --editor-stop: once play has run that many frames, the next one clicks Stop
    if (m_stopAfter && m_play.active() && ++m_playedFrames == *m_stopAfter)
        m_clickPlay = true;
    updateTitle();

    // Not while a value is being dragged: that would write the file every frame
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        savePreferencesIfChanged();
}

void EditorShell::drawUi(FrameContext& frame)
{
    const ScopeProfiler profiler{ "Editor UI" };
    m_drawing = true;
    updateStyle();
    ImGuizmo::BeginFrame();

    const int tint = m_context.playing() ? pushPlayTint() : 0;
    beginMainWindow();
    // --editor-fold: on the first frame, once ImGui read the layout file, which keeps the folded state too
    if (std::exchange(m_foldOnFirstFrame, false))
        m_workspace.foldInto(panels::inspector);
    m_workspace.drawDragTooltip(frame); // First, so a drop target's own tooltip replaces it
    m_actions.handleShortcuts(frame, m_viewport.givesInputToGame());
    if (m_context.renameRequested())
        m_workspace.reveal(panels::inspector); // The name field is there
    // In full viewport the menu bar, the status bar and the floating windows are not drawn
    const bool full = m_workspace.fullViewport();
    if (!full)
        drawMenuBar(frame);
    m_workspace.drawLayout(frame);
    std::function<void()> bar;
    if (m_document.recovery())
        bar = [this] { drawRecoveryBar(); };
    std::function<void()> topBar;
    if (full)
        topBar = [this, &frame] { drawFullViewportBar(frame); };
    m_viewport.draw(m_context, frame, bar, topBar);
    m_workspace.drawOverlays(frame);
    if (const auto scene = m_context.takeSceneRequest())
        m_document.request({ .kind = SceneAction::Kind::Open, .scene = *scene });
    if (!m_workspace.fullViewport())
    {
        m_workspace.drawRail();
        drawStatusBar(frame);
        drawStyleEditor();
    }
    drawOpenPopup(frame);
    drawSaveAsPopup(frame);
    drawUnsavedPrompt(frame);
    // Once the panels and the popups are drawn: these create, replace or destroy the world they show
    m_drawing = false;
    m_actions.runDeferred(frame);
    runSceneActions(frame);
    ImGui::End();
    ImGui::PopStyleColor(tint);
}

void EditorShell::runSceneActions(FrameContext& frame)
{
    // A restore that fails asks to reopen the scene: that runs in the same frame
    while (auto action = m_document.takeAction())
        run(*action, frame);
}

void EditorShell::run(const SceneAction& action, FrameContext& frame)
{
    assert(!m_drawing && "A scene action replaces the world while the panels draw: run it from runSceneActions");
    std::optional<std::uint64_t> opened;
    switch (action.kind)
    {
    case SceneAction::Kind::Quit:
        frame.engine.quit();
        return;
    case SceneAction::Kind::Restore:
        restoreAutosave(action.autosave, frame);
        return;
    case SceneAction::Kind::New:
        stopPlay(frame);
        frame.engine.newScene();
        break;
    case SceneAction::Kind::Open:
        stopPlay(frame);
        if (auto loaded = frame.engine.loadScene(assets::AssetId{ action.scene }); loaded)
            opened = action.scene;
        else
            log::warn("Cannot open the scene: {}", describe(loaded.error()));
        break;
    }
    m_document.opened(opened);
    rememberScene(frame.assets.database());
    m_viewport.resetCamera();
    offerRecovery(frame.assets.database());
}

auto EditorShell::currentScene() const -> std::optional<assets::AssetId>
{
    return m_document.scene().transform([](std::uint64_t scene) { return assets::AssetId{ scene }; });
}

void EditorShell::save(FrameContext& frame)
{
    const auto scene = currentScene();
    const auto* entry = scene ? frame.assets.database().find(*scene) : nullptr;
    if (!m_document.save(entry != nullptr))
        return;
    const auto file = frame.assets.database().absolutePath(*entry);
    if (auto written = writeSceneFile(m_context.editWorld()->registry(), frame.types, scene->value, file); !written)
    {
        log::warn("Cannot save the scene: {}", describe(written.error()));
        m_document.saveFailed();
        return;
    }
    m_document.saved();
    rememberScene(frame.assets.database());
}

auto EditorShell::saveAs(FrameContext& frame, std::string_view typed) -> std::expected<void, Error>
{
    const auto path = sceneFilePath(typed);
    if (!path)
        return std::unexpected{ path.error() };
    const auto& database = frame.assets.database();
    const auto file = database.roots().front().path / std::filesystem::path{ std::u8string{ path->begin(), path->end() } };
    std::error_code error;
    if (std::filesystem::exists(file, error))
        return makeError(ErrorCode::FileExists, std::format("'{}' exists already", *path));
    std::filesystem::create_directories(file.parent_path(), error);
    if (error)
        return makeError(ErrorCode::FileFailed, std::format("cannot create the folder of '{}': {}", *path, error.message()));
    const auto id = assets::AssetId::generate();
    if (auto written = writeSceneFile(m_context.editWorld()->registry(), frame.types, id.value, file); !written)
        return std::unexpected{ written.error() };
    auto added = frame.assets.addFile(*path);
    if (!added)
        return makeAssetError(std::move(added.error()), "saved, but the asset database does not take it");
    m_document.savedAs(added->value);
    rememberScene(database);
    return {};
}

auto EditorShell::sceneFileName() const -> std::string
{
    const auto scene = currentScene();
    const auto* entry = scene && m_engine ? m_engine->assets().database().find(*scene) : nullptr;
    return entry ? std::filesystem::path{ entry->path }.filename().string() : std::string{ "Untitled" };
}

auto EditorShell::sceneName() const -> std::string
{
    return std::format("{}{}", sceneFileName(), m_document.unsaved() ? "*" : "");
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
    const auto current = currentScene();
    std::optional<assets::AssetId> chosen;
    for (const auto& path : m_recent.paths)
    {
        if (ImGui::MenuItem(path.c_str(), nullptr, current && findProjectScene(database, path) == current))
            chosen = findProjectScene(database, path);
    }
    ImGui::EndMenu();
    if (chosen)
        m_document.request({ .kind = SceneAction::Kind::Open, .scene = chosen->value });
}

void EditorShell::rememberScene(const assets::AssetDatabase& database)
{
    const auto scene = currentScene();
    const auto* entry = scene ? database.find(*scene) : nullptr;
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
        log::warn("The recent scenes are not saved: {}", describe(saved.error()));
}

void EditorShell::drawOpenPopup(FrameContext& frame)
{
    if (std::exchange(m_openOpen, false))
    {
        m_openSearch.clear();
        m_openSelection = currentScene();
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
    searchField("##search", "Search", m_openSearch);

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
    bool open = false;
    {
        const theme::AccentFillScope fill;
        open = ImGui::Button("Open") || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
    }
    ImGui::EndDisabled();
    if (open && m_openSelection)
        chosen = m_openSelection;
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    if (chosen)
    {
        ImGui::CloseCurrentPopup();
        m_document.request({ .kind = SceneAction::Kind::Open, .scene = chosen->value });
    }
    ImGui::EndPopup();
}

void EditorShell::drawSaveAsPopup(FrameContext& frame)
{
    if (m_document.waiting() == SceneDocument::Wait::SaveAs && !ImGui::IsPopupOpen("Save Scene As"))
    {
        const auto scene = currentScene();
        const auto* entry = scene ? frame.assets.database().find(*scene) : nullptr;
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
            m_fileError = describe(saved.error());
    }
    ImGui::SameLine();
    const bool cancelled = ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape);
    if (cancelled)
        m_document.cancel(); // A prompt's Save that went through Save As drops its action
    if (done || cancelled)
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void EditorShell::drawUnsavedPrompt(FrameContext& frame)
{
    if (m_document.waiting() == SceneDocument::Wait::Prompt && !ImGui::IsPopupOpen("Unsaved Changes"))
        ImGui::OpenPopup("Unsaved Changes");
    if (!ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextUnformatted("The scene has unsaved changes. Save them first?");
    ImGui::Spacing();
    if (ImGui::Button("Save"))
    {
        ImGui::CloseCurrentPopup();
        save(frame); // Its action runs once saved, maybe through Save As; a failed save drops it
    }
    ImGui::SameLine();
    if (ImGui::Button("Don't Save"))
    {
        ImGui::CloseCurrentPopup();
        m_document.discard();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        ImGui::CloseCurrentPopup();
        m_document.cancel();
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
    assert(!m_drawing && "Play creates a world while the panels draw: make its action deferred");
    if (m_playWorld)
        return;
    // The game's code runs in this process from here on: a crash must not take the unsaved changes with it
    if (m_document.unsaved())
        autosave(frame);
    auto& play = frame.engine.createWorld();
    if (auto copied = copyWorld(frame.world.registry(), play.registry(), frame.types, resolveAssets(frame)); !copied)
    {
        log::warn("Cannot play: {}", describe(copied.error()));
        frame.engine.destroyWorld(play);
        return;
    }
    m_viewport.endGizmoDrag(m_context, frame); // An edit still being dragged ends here
    m_context.undoStack().seal();
    m_context.enterPlay(play);
    m_playWorld = &play;
    m_playedFrames = 0;
    m_play.play();
    m_viewport.setGameView(true);
    m_workspace.play(m_context.preferences().fullViewportOnPlay);
}

void EditorShell::stopPlay(FrameContext& frame)
{
    assert(!m_drawing && "Stop destroys a world while the panels draw: make its action deferred");
    if (!m_playWorld)
        return;
    m_context.exitPlay();
    frame.engine.destroyWorld(*m_playWorld);
    m_playWorld = nullptr;
    ++m_stops;
    m_play.stop();
    m_viewport.setGameView(false);
    frame.engine.restoreCursor(); // The game may have hidden it
    m_workspace.stop();
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
        log::warn("The editor preferences are not saved: {}", describe(saved.error()));
}

void EditorShell::autosave(FrameContext& frame)
{
    if (auto written = m_document.autosave(frame.types, std::chrono::steady_clock::now()); !written)
        log::warn("The scene is not autosaved: {}", describe(written.error()));
}

void EditorShell::offerRecovery(const assets::AssetDatabase& database)
{
    const auto scene = currentScene();
    const auto* entry = scene ? database.find(*scene) : nullptr;
    m_document.offerRecovery(entry ? database.absolutePath(*entry) : std::filesystem::path{});
}

void EditorShell::drawRecoveryBar()
{
    const auto* zone = std::chrono::current_zone();
    const auto local = zone->to_local(std::chrono::clock_cast<std::chrono::system_clock>(m_document.recovery()->written));
    const auto today = std::chrono::floor<std::chrono::days>(zone->to_local(std::chrono::system_clock::now()));
    const auto label = recoveryLabel(sceneFileName(), std::chrono::floor<std::chrono::seconds>(local), today);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::warning, "%s", theme::icons::recovery);
    ImGui::SameLine();
    ImGui::TextUnformatted(label.c_str());
    ImGui::SameLine();
    {
        const theme::AccentFillScope fill;
        if (ImGui::Button("Restore"))
            m_document.restoreRecovery(); // Runs once the panels are drawn (runSceneActions)
    }
    ImGui::SetItemTooltip("Open the autosaved changes as the scene, unsaved");
    ImGui::SameLine();
    if (ImGui::Button("Discard"))
        m_document.discardRecovery();
    ImGui::SetItemTooltip("Delete the autosave");
}

void EditorShell::restoreAutosave(const std::filesystem::path& file, FrameContext& frame)
{
    assert(!m_drawing && "Restore replaces the world while the panels draw: run it from runSceneActions");
    const auto text = readAutosave(file);
    if (!text)
    {
        log::warn("Cannot restore the autosave: {}", describe(text.error()));
        return;
    }
    const auto scene = serialize::parseScene(*text, frame.types);
    if (!scene)
    {
        log::warn("Cannot restore the autosave '{}': {}", file.string(), serialize::describe(scene.error()));
        return;
    }
    stopPlay(frame);
    frame.engine.newScene();
    if (auto created = serialize::instantiate(frame.world.registry(), frame.types, *scene, resolveAssets(frame)); !created)
    {
        // Nothing was created: the scene as its file has it again (runSceneActions reopens it), and the bar with it
        log::warn("Cannot restore the autosave '{}': {}", file.string(), serialize::describe(created.error()));
        m_document.restoreFailed();
        return;
    }
    m_document.restored();
}

void EditorShell::drawStatusBar(FrameContext& frame)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float height = theme::statusBarHeight();
    ImGui::SetNextWindowPos(ImVec2{ viewport->Pos.x, viewport->Pos.y + viewport->Size.y - height });
    ImGui::SetNextWindowSize(ImVec2{ viewport->Size.x, height });
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar;
    // A frame's row centered in the bar: the inset above and below it
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 8.0f * theme::scale(), (height - ImGui::GetFrameHeight()) * 0.5f });
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::bgDeep);
    const bool visible = ImGui::Begin(statusBarWindow, nullptr, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (visible)
    {
        ImGui::AlignTextToFramePadding(); // Its text and buttons centered in the bar, as in the menu bar
        drawPanelButtons();
        // A slim line apart from the selection
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        ImGui::SameLine(0.0f, gap);
        const ImVec2 line = ImGui::GetCursorScreenPos();
        drawBarLine(ImGui::GetWindowDrawList(), line.x, line.y + ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset);
        ImGui::Dummy(ImVec2{ std::max(1.0f, std::round(theme::scale())), ImGui::GetTextLineHeight() });
        ImGui::SameLine(0.0f, gap);
        ImGui::TextUnformatted(selectionLabel().c_str());
        ImGui::SameLine();
        const float left = ImGui::GetCursorPosX();

        // The key hints centered between the selection and the timings, which keep their place as the numbers change
        const float timings = drawTimings(frame, ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x);
        drawKeyHints(keyHints(), left, timings - ImGui::GetWindowPos().x);
    }
    ImGui::End();
}

auto EditorShell::drawTimings(FrameContext& frame, float right) -> float
{
    // Rolling averages: the frame's time, the part of it the renderer waited for the GPU and the display (the rest is
    // the CPU's work), and the frame graph's passes on the GPU
    const double frameMs = Profiler::instance().time("Frame Time");
    const double cpuMs = std::max(frameMs - Profiler::instance().time("Frame Wait"), 0.0);
    double gpuMs = 0.0;
    for (const auto& timing : frame.frameGraph.timings())
        gpuMs += timing.gpuMs;
    m_gpuMs.add(gpuMs);
    const double fps = frameMs > 0.0 ? 1000.0 / frameMs : 0.0;
    const std::array slots{
        TimingSlot{ .label = "", .number = std::format("{:.0f}", fps), .unit = " fps", .widest = "000" },
        TimingSlot{ .label = "CPU ", .number = std::format("{:.2f}", cpuMs), .unit = " ms", .widest = "00.00" },
        TimingSlot{ .label = "GPU ", .number = std::format("{:.2f}", m_gpuMs.average()), .unit = " ms", .widest = "00.00" },
    };

    // Right to left, each slot's label, number and unit together at its right end (the room kept for a wider number is
    // at its left), a slim line between two slots
    const TabularText tabular;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float gap = style.ItemSpacing.x;
    const float lineWidth = std::max(1.0f, std::round(theme::scale()));
    const float fontSize = ImGui::GetFontSize();
    const float top = ImGui::GetCursorScreenPos().y + ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset;
    const ImU32 muted = theme::packed(theme::textMuted);
    const ImU32 value = theme::packed(theme::textDim);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    float x = right;
    for (auto slot = slots.rbegin(); slot != slots.rend(); ++slot)
    {
        if (slot != slots.rbegin())
        {
            x -= gap + lineWidth;
            drawBarLine(drawList, x, top);
            x -= gap;
        }
        const float labelWidth = tabular.width(slot->label);
        const float unitWidth = tabular.width(slot->unit);
        const float numberWidth = tabular.width(slot->number);
        const float slotWidth = labelWidth + std::max(numberWidth, tabular.width(slot->widest)) + unitWidth;
        x -= slotWidth;
        tabular.draw(x + slotWidth - unitWidth - numberWidth - labelWidth, top, muted, slot->label);
        tabular.draw(x + slotWidth - unitWidth - numberWidth, top, value, slot->number);
        tabular.draw(x + slotWidth - unitWidth, top, muted, slot->unit);
    }
    if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(ImVec2{ x, top }, ImVec2{ right, top + fontSize }))
        ImGui::SetTooltip("Frame %.2f ms (%.0f fps)\nCPU: the frame less the time it waited for the GPU and the display\nGPU: the render graph's passes", frameMs, fps);
    return x;
}

void EditorShell::drawPanelButtons()
{
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });
    const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float near = std::round(style.ItemInnerSpacing.x * 0.5f);
    const float apart = style.ItemSpacing.x;
    bool first = true;
    for (const auto& panel : m_workspace.panels())
    {
        if (!panel->statusButton())
            continue;
        if (!std::exchange(first, false))
            ImGui::SameLine();
        // The icon and the name, then each count after its icon in its level's color
        const std::string name{ panel->name() };
        std::vector<LabelPart> parts{ { panel->icon(), text, 0.0f }, { name, text, near } };
        for (const auto& badge : panel->statusBadges(m_context))
        {
            const ImU32 color = theme::packed(badge.level == log::Level::Warn ? theme::warning : theme::error);
            parts.push_back({ badge.icon, color, apart });
            parts.push_back({ std::to_string(badge.count), color, near });
        }
        if (labelButton(name.c_str(), parts, m_workspace.drawers().isShown(name)))
            m_workspace.toggleDrawer(name);
        const auto action = m_actions.tooltip(panel->toggleAction());
        const auto more = panel->statusTooltip(m_context);
        const auto tooltip = more.empty() ? action : std::format("{}\n{}", action, more);
        ImGui::SetItemTooltip("%s", tooltip.c_str());
    }
    ImGui::PopStyleColor();
}

auto EditorShell::keyHints() const -> std::vector<KeyHint>
{
    // The viewport's own keys are no actions (they depend on its focus); the others come from the registry
    if (m_viewport.flying())
        return { { "W A S D", "move" }, { "Q", "down" }, { "E", "up" }, { "Wheel", "speed" } };
    if (m_viewport.hovered())
    {
        if (m_viewport.gameView())
            return { { "Click", "give the game the input" }, { m_actions.shortcut("play.release"), "take it back" } };
        return { { "Q", "select" }, { "W", "move" }, { "E", "rotate" }, { "R", "scale" }, { m_actions.shortcut("edit.focus"), "focus" }, { "RMB", "fly" }, { "Alt+LMB", "orbit" }, { "MMB", "pan" } };
    }
    if (m_workspace.hovers(panels::scene))
        return { { m_actions.shortcut("edit.rename"), "rename" }, { "Drag", "reparent" }, { m_actions.shortcut("edit.duplicate"), "duplicate" }, { m_actions.shortcut("edit.delete"), "delete" } };
    return {};
}

void EditorShell::drawKeyHints(const std::vector<KeyHint>& hints, float left, float right)
{
    if (hints.empty())
        return;
    // Each key in the text color and what it does muted, the pairs apart by a wider gap
    const ImGuiStyle& style = ImGui::GetStyle();
    const float gap = style.ItemSpacing.x * 2.0f;
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

void EditorShell::buildActions()
{
    const auto chord = [](Key key, bool ctrl = true, bool shift = false, bool alt = false) { return Chord{ .key = key, .ctrl = ctrl, .shift = shift, .alt = alt }; };
    const auto hasSelection = [this] { return static_cast<bool>(m_context.selectedEntity()); };

    m_actions.add({ .id = "file.new", .label = "New", .chord = chord(Key::N), .deferred = true, .run = [this](FrameContext&) { m_document.request({ .kind = SceneAction::Kind::New }); } });
    m_actions.add({ .id = "file.open", .label = "Open...", .chord = chord(Key::O), .deferred = true, .run = [this](FrameContext&) { m_openOpen = true; } });
    m_actions.add({ .id = "file.save", .label = "Save", .chord = chord(Key::S), .run = [this](FrameContext& frame) { save(frame); } });
    m_actions.add({ .id = "file.saveAs", .label = "Save As...", .chord = chord(Key::S, true, true), .run = [this](FrameContext&) { m_document.requestSaveAs(); } });

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
    m_actions.add({ .id = "edit.preferences", .label = "Preferences...", .run = [this](FrameContext&) { m_workspace.open(panels::preferences); } });
    m_actions.add({ .id = "edit.rename",
        .label = "Rename",
        .chord = chord(Key::F2, false),
        .enabled = hasSelection,
        .run = [this](FrameContext&) { m_context.requestRename(); } });
    m_actions.add({ .id = "edit.focus", .label = "Focus", .chord = chord(Key::F, false), .enabled = hasSelection, .run = [this](FrameContext&) { m_context.requestFocus(); } });

    // The drawer panels' toggle actions, each named after its panel
    const std::array<std::pair<std::string_view, Chord>, 2> drawerChords{ { { "view.content", chord(Key::Space) }, { "view.console", chord(Key::Space, true, true) } } };
    for (const auto& panel : m_workspace.panels())
    {
        const auto id = panel->toggleAction();
        if (id.empty())
            continue;
        const auto bound = std::ranges::find(drawerChords, id, &std::pair<std::string_view, Chord>::first);
        m_actions.add({ .id = std::string{ id },
            .label = std::string{ panel->name() },
            .chord = bound != drawerChords.end() ? std::optional{ bound->second } : std::nullopt,
            .run = [this, name = std::string{ panel->name() }](FrameContext&) { m_workspace.toggleDrawer(name); } });
    }
    m_actions.add({ .id = "view.foldColumn", .label = "Fold Side Column", .chord = chord(Key::B), .run = [this](FrameContext&) { m_workspace.toggleFold(); } });
    m_actions.add({ .id = "view.fullViewport", .label = "Full Viewport", .chord = chord(Key::F11, false), .run = [this](FrameContext&) { m_workspace.toggleFullViewport(); } });

    m_actions.add({ .id = "help.shortcuts", .label = "Keyboard Shortcuts", .run = [this](FrameContext&) { m_workspace.open(panels::shortcuts); } });
    m_actions.add({ .id = "help.about", .label = "About", .run = [this](FrameContext&) { m_workspace.open(panels::about); } });

    m_actions.add({ .id = "play.toggle", .label = "Play", .chord = chord(Key::P), .deferred = true, .run = [this](FrameContext& frame) { m_play.active() ? stopPlay(frame) : startPlay(frame); } });
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

void EditorShell::drawFullViewportBar(FrameContext& frame)
{
    drawPlayButtons(frame);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
    // The way back stays in sight: the button, and its shortcut next to it
    const auto shortcut = m_actions.shortcut("view.fullViewport");
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });
    const bool leave = barButton(theme::icons::leaveFullViewport);
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("%s", std::format("Leave full viewport ({}, Esc)", shortcut).c_str());
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextDisabled("%s", shortcut.c_str());
    if (leave)
        runAction("view.fullViewport", frame);
}

void EditorShell::drawStyleEditor()
{
    if (m_demoWindow)
        ImGui::ShowDemoWindow(&m_demoWindow); // Every widget, for reference while building panels
    if (!m_styleEditor)
        return;
    if (ImGui::Begin("Style Editor", &m_styleEditor))
        ImGui::ShowStyleEditor();
    ImGui::End();
}

void EditorShell::drawMenuBar(FrameContext& frame)
{
    // The padding sets the bar's height and where its text sits; its menus and their popups keep the usual one
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, menuBarPadding());
    const bool open = ImGui::BeginMainMenuBar();
    ImGui::PopStyleVar();
    if (!open)
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
        for (const auto& panel : m_workspace.panels())
        {
            if (panel->menu() != Menu::View)
                continue;
            const std::string name{ panel->name() };
            if (panel->placement() == Placement::Drawer)
            {
                // Opens the drawer (its button and chord also close it)
                const auto chord = m_actions.shortcut(panel->toggleAction());
                if (ImGui::MenuItem(name.c_str(), chord.c_str(), m_workspace.drawers().isShown(name) || m_workspace.drawers().isPinned(name)))
                    m_workspace.showDrawer(name);
                continue;
            }
            if (ImGui::MenuItem(name.c_str(), nullptr, panel->isOpen()))
                panel->setOpen(!panel->isOpen());
        }
        ImGui::Separator();
        m_actions.menuItem("view.foldColumn", frame, {}, m_workspace.folded());
        m_actions.menuItem("view.fullViewport", frame, {}, m_workspace.fullViewport());
        if (ImGui::MenuItem("Reset Layout"))
            m_workspace.resetLayout();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Debug"))
    {
        for (const auto& panel : m_workspace.panels())
        {
            if (panel->menu() != Menu::Debug)
                continue;
            // A drawer panel shows in the drawer (it closes as the drawer does), or comes to the front pinned (its tab
            // closes it); another is checked while open and toggles
            const std::string name{ panel->name() };
            if (panel->placement() == Placement::Drawer)
            {
                if (ImGui::MenuItem(name.c_str()))
                    m_workspace.showDrawer(name);
            }
            else if (ImGui::MenuItem(name.c_str(), nullptr, panel->isOpen()))
                panel->setOpen(!panel->isOpen());
        }
        ImGui::MenuItem("ImGui Demo", nullptr, &m_demoWindow);
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
    const char* fold = m_workspace.folded() ? theme::icons::unfoldColumn : theme::icons::foldColumn;
    const float buttons = ImGui::CalcTextSize(fold).x + ImGui::CalcTextSize(theme::icons::fullViewport).x + 4.0f * style.FramePadding.x + style.ItemSpacing.x;
    // The buttons at the right end, and the name left of them when there is room for both; the name goes first
    const float nameX = right - ImGui::CalcTextSize(name.c_str()).x;
    const bool showName = nameX - style.ItemSpacing.x - buttons > ImGui::GetCursorPosX();
    const float buttonsX = showName ? nameX - style.ItemSpacing.x - buttons : right - buttons;
    if (buttonsX <= ImGui::GetCursorPosX())
        return; // No room next to the menus and play controls
    ImGui::SetCursorPosX(buttonsX);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });
    if (barButton(fold))
        runAction("view.foldColumn", frame);
    const auto foldTooltip = m_workspace.folded() ? std::format("Unfold Side Column ({})", m_actions.shortcut("view.foldColumn")) : m_actions.tooltip("view.foldColumn");
    ImGui::SetItemTooltip("%s", foldTooltip.c_str());
    if (barButton(theme::icons::fullViewport))
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
    m_actions.run(id, frame);
}

void EditorShell::drawPlayControls(FrameContext& frame)
{
    // The three buttons centered on the bar; the play state's label hangs off their right
    const auto& style = ImGui::GetStyle();
    const char* toggle = m_play.state() == PlaySession::State::Stopped ? theme::icons::play : theme::icons::stop;
    float width = 6.0f * style.FramePadding.x + 2.0f * style.ItemSpacing.x;
    for (const char* icon : { toggle, theme::icons::pause, theme::icons::step })
        width += ImGui::CalcTextSize(icon).x;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), std::round((ImGui::GetWindowWidth() - width) * 0.5f)));
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
        const bool clicked = barButton(icon);
        ImGui::PopStyleColor(colors);
        ImGui::PopID();
        ImGui::SetItemTooltip("%s", m_actions.tooltip(action).c_str());
        return clicked;
    };
    const auto run = [&](std::string_view id) { runAction(id, frame); };

    const bool active = state != PlaySession::State::Stopped;
    // --editor-play and --editor-stop click it here, where the user does: in full viewport, while the viewport draws
    const bool scripted = std::exchange(m_clickPlay, false);
    if (button(active ? theme::icons::stop : theme::icons::play, "play", "play.toggle", false, theme::text, active) || scripted)
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
