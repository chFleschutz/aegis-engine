module;

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

// The default layout in the dockspace 'dockspace' of 'size': the hierarchy on the left, the inspector on the right,
// the assets and the console tabbed at the bottom, the viewport in the middle. The debug panels (renderer, profiler,
// statistics) start closed and join the bottom tabs when opened: they are wide tables.
void buildDefaultLayout(ImGuiID dockspace, ImVec2 size)
{
    constexpr float leftShare = 0.18f;
    constexpr float rightShare = 0.22f;
    constexpr float bottomShare = 0.25f;

    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, size);

    ImGuiID center = dockspace;
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, bottomShare, nullptr, &center);
    const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, leftShare, nullptr, &center);
    const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, rightShare / (1.0f - leftShare), nullptr, &center);

    const auto dock = [](std::initializer_list<const char*> windows, ImGuiID node) {
        for (const char* window : windows)
            ImGui::DockBuilderDockWindow(window, node);
        ImGui::DockBuilderGetNode(node)->SelectedTabId = ImGui::GetID(*windows.begin()); // The first one shows
    };
    dock({ "Hierarchy" }, left);
    dock({ "Inspector" }, right);
    dock({ "Assets", "Console", "Renderer", "Profiler", "Statistics" }, bottom);
    dock({ "Viewport" }, center);
    ImGui::DockBuilderFinish(dockspace);
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
            panel->setOpen(panel->docked()); // Not the ImGui demo: it floats over the viewport
    }
    // A headless run (a test, a screenshot) neither reads nor changes the user's layout.
    if (!engine.headless() && !engine.project().empty())
        m_layoutFile = layoutFile(engine.project());
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
    updateStyle();
    return {};
}

void EditorShell::onShutdown()
{
    savePreferencesIfChanged();
    // ImGui would save on destroying its context, after this shell (and the file name) are gone.
    if (m_layoutFile.empty())
        return;
    ImGui::SaveIniSettingsToDisk(m_layoutFile.c_str());
    ImGui::GetIO().IniFilename = nullptr;
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

    updateStyle();
    ImGuizmo::BeginFrame();

    const int tint = m_context.playing() ? pushPlayTint() : 0;
    beginMainWindow();
    m_actions.handleShortcuts(frame, m_viewport.givesInputToGame());
    if (m_context.renameRequested())
        openPanel("Inspector"); // The name field is there
    drawMenuBar(frame);
    drawDockspace();
    for (const auto& panel : m_panels)
    {
        if (panel->isOpen())
            panel->draw(m_context, frame);
    }
    if (m_preferencesWindow->isOpen())
        m_preferencesWindow->draw(m_context, frame);
    m_viewport.draw(m_context, frame);
    if (const auto scene = m_context.takeSceneRequest())
        request({ .kind = PendingAction::Kind::Open, .scene = assets::AssetId{ *scene } }, frame);
    drawStatusBar(frame);
    drawOpenPopup(frame);
    drawSaveAsPopup(frame);
    drawUnsavedPrompt(frame);
    drawStyleEditor();
    ImGui::End();
    ImGui::PopStyleColor(tint);
    updateWorlds(frame);
    updateTitle();

    // Not while a value is being dragged: that would write the file every frame
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        savePreferencesIfChanged();
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
    m_context.resetScene();
    m_viewport.resetCamera();
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
    if (auto written = writeSceneFile(m_context.editWorld()->registry(), frame.types, id.value, file); !written)
        return std::unexpected{ written.error() };
    auto added = frame.assets.addFile(*path);
    if (!added)
        return std::unexpected{ std::format("Saved, but the asset database does not take it: {}", added.error()) };
    m_scene = *added;
    m_context.markSaved();
    return {};
}

auto EditorShell::sceneName() const -> std::string
{
    const auto* entry = m_scene && m_engine ? m_engine->assets().database().find(*m_scene) : nullptr;
    const auto name = entry ? std::filesystem::path{ entry->path }.filename().string() : std::string{ "Untitled" };
    return std::format("{}{}", name, m_context.unsaved() ? "*" : "");
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
    ImGui::Separator();
    m_actions.menuItem("file.save", frame);
    m_actions.menuItem("file.saveAs", frame);
    ImGui::EndMenu();
}

void EditorShell::drawOpenPopup(FrameContext& frame)
{
    if (std::exchange(m_openOpen, false))
        ImGui::OpenPopup("Open Scene");
    ImGui::SetNextWindowSize(ImVec2{ 420.0f * theme::scale(), 360.0f * theme::scale() }, ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Open Scene"))
        return;
    const auto& database = frame.assets.database();
    std::vector<const assets::AssetEntry*> scenes;
    for (const auto& entry : database.entries())
    {
        if (entry.type == "Scene" && !database.roots()[entry.root].readOnly)
            scenes.push_back(&entry);
    }
    std::ranges::sort(scenes, {}, &assets::AssetEntry::path);

    std::optional<assets::AssetId> chosen;
    const float buttons = ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginChild("##scenes", ImVec2{ 0.0f, -buttons }, ImGuiChildFlags_Borders))
    {
        for (const auto* entry : scenes)
        {
            if (ImGui::Selectable(entry->path.c_str(), m_scene == entry->id, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                chosen = entry->id;
        }
        if (scenes.empty())
            ImGui::TextDisabled("The project has no scene files");
    }
    ImGui::EndChild();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    ImGui::TextDisabled("Double-click a scene to open it");
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
    auto& play = frame.engine.createWorld();
    const auto prepare = [&frame](const reflect::TypeInfo& type, void* component) { frame.assets.resolve(type, component, frame.types); };
    if (auto copied = copyWorld(frame.world.registry(), play.registry(), frame.types, prepare); !copied)
    {
        log::warn("Cannot play: {}", copied.error());
        frame.engine.destroyWorld(play);
        return;
    }
    m_context.undoStack().seal(); // An edit still being dragged ends here
    m_context.enterPlay(play);
    m_playWorld = &play;
    m_play.play();
    m_viewport.setGameView(true);
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
        ImGui::TextUnformatted(selectionLabel().c_str());

        const double frameMs = Profiler::instance().time("Frame Time"); // Its rolling average
        double gpuMs = 0.0;
        for (const auto& timing : frame.frameGraph.timings())
            gpuMs += timing.gpuMs;
        m_gpuMs.add(gpuMs);
        const auto timing = std::format("{:.1f} fps ({:.2f} ms)  |  GPU {:.2f} ms", frameMs > 0.0 ? 1000.0 / frameMs : 0.0, frameMs, m_gpuMs.average());
        ImGui::SameLine((ImGui::GetWindowWidth() - ImGui::CalcTextSize(timing.c_str()).x) * 0.5f);
        ImGui::TextUnformatted(timing.c_str());

        drawProblemCounts();
    }
    ImGui::End();
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
    const ImGuiStyle& style = ImGui::GetStyle();
    const float width = ImGui::CalcTextSize(warnings.c_str()).x + ImGui::CalcTextSize(errors.c_str()).x + 4.0f * style.FramePadding.x + style.ItemSpacing.x;
    ImGui::SameLine(ImGui::GetWindowWidth() - style.WindowPadding.x - width);

    const auto problem = m_context.console().lastProblem();
    const auto tooltip = problem ? std::format("{}: {}", log::levelName(problem->level), problem->message) : std::string{ "No warnings or errors" };
    // A count of zero is dim: only the levels that have messages draw the eye
    const auto count = [&](const std::string& label, std::size_t value, const ImVec4& color) {
        ImGui::PushStyleColor(ImGuiCol_Text, value > 0 ? color : theme::textDisabled);
        const bool clicked = ImGui::SmallButton(label.c_str());
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s", tooltip.c_str());
        if (clicked)
        {
            openPanel("Console");
            m_context.requestConsoleProblems();
        }
    };
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });
    count(warnings, counts.warnings, theme::warning);
    ImGui::SameLine();
    count(errors, counts.errors, theme::error);
    ImGui::PopStyleColor();
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
    if (std::exchange(m_resetLayout, false) || !ImGui::DockBuilderGetNode(id))
        buildDefaultLayout(id, size);
    // The panels' tabs have their own close buttons, and the View menu lists them: no window menu or close-all per node
    ImGui::DockSpace(id, size, ImGuiDockNodeFlags_NoWindowMenuButton | ImGuiDockNodeFlags_NoCloseButton);
}

void EditorShell::openPanel(std::string_view name)
{
    for (const auto& panel : m_panels)
    {
        if (panel->name() == name)
            panel->setOpen(true);
    }
}

void EditorShell::resetLayout()
{
    m_resetLayout = true;
    for (auto* panel : m_defaultOpen)
        panel->setOpen(true);
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
        m_actions.menuItem("edit.undo", frame, editLabel("Undo", m_context.undoStack().nextUndo()));
        m_actions.menuItem("edit.redo", frame, editLabel("Redo", m_context.undoStack().nextRedo()));
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
            if (ImGui::MenuItem(name.c_str(), nullptr, panel->isOpen()))
                panel->setOpen(!panel->isOpen());
        }
        ImGui::Separator();
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
            if (ImGui::MenuItem(name.c_str(), nullptr, panel->isOpen()))
                panel->setOpen(!panel->isOpen());
        }
        ImGui::MenuItem("Style Editor", nullptr, &m_styleEditor);
        ImGui::EndMenu();
    }

    drawPlayControls(frame);
    drawSceneName();
    ImGui::EndMainMenuBar();
}

auto EditorShell::editLabel(std::string_view verb, const Edit* edit) const -> std::string
{
    if (!edit)
        return std::string{ verb };
    return std::format("{} {}", verb, editTitle(*edit, m_context.world()->registry()));
}

void EditorShell::drawSceneName()
{
    const auto name = sceneName();
    const auto& style = ImGui::GetStyle();
    const float x = ImGui::GetWindowWidth() - ImGui::CalcTextSize(name.c_str()).x - style.WindowPadding.x;
    if (x <= ImGui::GetCursorPosX())
        return; // No room next to the menus and play controls
    ImGui::SetCursorPosX(x);
    ImGui::TextUnformatted(name.c_str());
}

void EditorShell::drawPlayControls(FrameContext& frame)
{
    const auto state = m_play.state();
    const auto& style = ImGui::GetStyle();
    const float buttonWidth = ImGui::CalcTextSize(theme::icons::play).x + 2.0f * style.FramePadding.x;
    const float width = 3.0f * buttonWidth + 2.0f * style.ItemSpacing.x;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), (ImGui::GetWindowWidth() - width) * 0.5f));

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
    const auto run = [&](std::string_view id) {
        if (const auto* action = m_actions.find(id); action && (!action->enabled || action->enabled()))
            action->run(frame);
    };

    const bool active = state != PlaySession::State::Stopped;
    if (button(active ? theme::icons::stop : theme::icons::play, "play", "play.toggle", false, theme::text, active))
        run("play.toggle");
    ImGui::BeginDisabled(!m_play.active());
    if (button(theme::icons::pause, "pause", "play.pause", state == PlaySession::State::Paused, theme::accentHover))
        run("play.pause");
    ImGui::EndDisabled();
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
