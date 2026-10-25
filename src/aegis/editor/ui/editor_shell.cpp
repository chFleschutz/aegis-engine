module;

#include <imgui.h>
#include <imgui_stdlib.h>
#include <ImGuizmo.h>

module aegis.editor;
import std;
import aegis.assets;
import aegis.core;
import aegis.reflect;
import aegis.world;
import :panels;

namespace aegis::editor
{
namespace
{
// Today's look, until the editor's theme (docs/editor.md, "Visual style") replaces it.
void applyStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.WindowBorderSize = 0.0f;
    style.PopupRounding = 4.0f;
    style.FrameRounding = 2.0f;
    style.TouchExtraPadding = ImVec2(2.0f, 2.0f);
    style.TabBarBorderSize = 0.0f;
    style.TabRounding = 4.0f;
    style.DockingSeparatorSize = 0.0f;
}

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
    constexpr ImVec4 tint{ 0.45f, 0.22f, 0.05f, 1.0f };
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
}

EditorShell::EditorShell()
{
    // The console sees every message from here on, and the log still writes to stderr.
    m_logSink = log::addSink([this](log::Level level, std::string_view message) { m_context.console().push(level, message); });

    m_panels.push_back(makeScenePanel());
    m_panels.push_back(makeInspectorPanel());
    m_panels.push_back(makeAssetPanel());
    m_panels.push_back(makeConsolePanel());
    m_panels.push_back(makePreferencesPanel());
    m_panels.push_back(makeRendererPanel());
    m_panels.push_back(makeStatisticsPanel());
    m_panels.push_back(makeProfilerPanel());
    m_panels.push_back(makeDemoPanel());
}

EditorShell::~EditorShell()
{
    log::removeSink(m_logSink);
}

auto EditorShell::onStartup(Engine& engine) -> std::expected<void, EngineError>
{
    m_engine = &engine;
    m_scene = engine.scene();
    applyStyle();
    // A drag in a window's body is the viewport's (orbit, gizmo), not a window move
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    if (engine.desc().editorPanels == "all")
    {
        for (const auto& panel : m_panels)
            panel->setOpen(true);
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
    if (m_viewport.givesInputToGame())
        return {}; // The play world's game has it
    const bool viewport = m_viewport.capturesInput();
    return { .keyboard = io.WantCaptureKeyboard || viewport, .mouse = io.WantCaptureMouse || viewport };
}

void EditorShell::onFrame(FrameContext& frame)
{
    if (!m_context.playing())
        m_context.setWorld(frame.world);
    m_context.selection().prune(m_context.world()->registry());

    ImGui::GetStyle().FontScaleMain = m_context.preferences().uiScale;
    ImGuizmo::BeginFrame();

    const int tint = m_context.playing() ? pushPlayTint() : 0;
    beginMainWindow();
    handleShortcuts(frame);
    drawMenuBar(frame);
    for (const auto& panel : m_panels)
    {
        if (panel->isOpen())
            panel->draw(m_context, frame);
    }
    m_viewport.draw(m_context, frame);
    if (const auto scene = m_context.takeSceneRequest())
        request({ .kind = PendingAction::Kind::Open, .scene = assets::AssetId{ *scene } }, frame);
    drawStatusBar(frame);
    drawOpenPopup(frame);
    drawSaveAsPopup(frame);
    drawUnsavedPrompt(frame);
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

void EditorShell::updateTitle()
{
    const auto* entry = m_scene && m_engine ? m_engine->assets().database().find(*m_scene) : nullptr;
    const auto name = entry ? std::filesystem::path{ entry->path }.filename().string() : std::string{ "Untitled" };
    auto title = std::format("{}{} - Aegis Editor", name, m_context.unsaved() ? "*" : "");
    if (title == m_title || !m_engine)
        return;
    m_title = std::move(title);
    m_engine->setTitle(m_title);
}

void EditorShell::drawFileMenu(FrameContext& frame)
{
    if (!ImGui::BeginMenu("File"))
        return;
    if (ImGui::MenuItem("New", "Ctrl+N"))
        request({ .kind = PendingAction::Kind::New }, frame);
    if (ImGui::MenuItem("Open...", "Ctrl+O"))
        m_openOpen = true;
    ImGui::Separator();
    if (ImGui::MenuItem("Save", "Ctrl+S"))
        save(frame);
    if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S"))
        m_openSaveAs = true;
    ImGui::EndMenu();
}

void EditorShell::drawOpenPopup(FrameContext& frame)
{
    if (std::exchange(m_openOpen, false))
        ImGui::OpenPopup("Open Scene");
    ImGui::SetNextWindowSize(ImVec2{ 420.0f, 360.0f }, ImGuiCond_Appearing);
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
    ImGui::SetNextItemWidth(360.0f);
    const bool entered = ImGui::InputText("##path", &m_saveAsPath, ImGuiInputTextFlags_EnterReturnsTrue);
    if (!m_fileError.empty())
        ImGui::TextColored(ImVec4{ 1.0f, 0.4f, 0.4f, 1.0f }, "%s", m_fileError.c_str());
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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 8.0f, 2.0f });
    const bool visible = ImGui::Begin("##StatusBar", nullptr, flags);
    ImGui::PopStyleVar();
    if (visible)
    {
        const double frameMs = Profiler::instance().time("Frame Time");
        double gpuMs = 0.0;
        for (const auto& timing : frame.frameGraph.timings())
            gpuMs += timing.gpuMs;

        ImGui::Text("%zu selected  |  %.1f fps (%.2f ms)  |  GPU %.2f ms", m_context.selection().ids().size(), frameMs > 0.0 ? 1000.0 / frameMs : 0.0, frameMs, gpuMs);

        if (const auto problem = m_context.console().lastProblem())
        {
            const bool isWarning = problem->level == log::Level::Warn;
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, isWarning ? ImVec4{ 1.0f, 0.82f, 0.3f, 1.0f } : ImVec4{ 1.0f, 0.4f, 0.4f, 1.0f });
            ImGui::TextUnformatted(std::format("|  {}: {}", log::levelName(problem->level), problem->message).c_str());
            ImGui::PopStyleColor();
        }

        if (m_context.unsaved())
        {
            const char* marker = "* unsaved";
            ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(marker).x - ImGui::GetStyle().WindowPadding.x);
            ImGui::TextUnformatted(marker);
        }
    }
    ImGui::End();
}

void EditorShell::handleShortcuts(FrameContext& frame)
{
    // Not while a text box has the keyboard: it has its own undo
    if (ImGui::GetIO().WantTextInput)
        return;
    const bool ctrl = ImGui::GetIO().KeyCtrl;
    const bool shift = ImGui::GetIO().KeyShift;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z) && !shift)
        undoEdit(m_context, frame);
    else if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_Y) || (shift && ImGui::IsKeyPressed(ImGuiKey_Z))))
        redoEdit(m_context, frame);
    else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
        duplicateSelected(m_context, frame);
    else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false))
        request({ .kind = PendingAction::Kind::New }, frame);
    else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O, false))
        m_openOpen = true;
    else if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_S, false))
        m_openSaveAs = true;
    else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
        save(frame);
    else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_P, false))
    {
        if (ImGui::GetIO().KeyAlt)
            m_play.step();
        else if (shift && m_play.state() == PlaySession::State::Playing)
            m_play.pause();
        else if (shift)
            m_play.play();
        else if (m_play.active())
            stopPlay(frame);
        else
            startPlay(frame);
    }
}

void EditorShell::drawMenuBar(FrameContext& frame)
{
    if (!ImGui::BeginMainMenuBar())
        return;

    drawFileMenu(frame);

    if (ImGui::BeginMenu("Edit"))
    {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_context.undoStack().canUndo()))
            undoEdit(m_context, frame);
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, m_context.undoStack().canRedo()))
            redoEdit(m_context, frame);
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, static_cast<bool>(m_context.selectedEntity())))
            duplicateSelected(m_context, frame);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Play"))
    {
        const auto state = m_play.state();
        if (ImGui::MenuItem(state == PlaySession::State::Stopped ? "Play" : "Stop", "Ctrl+P"))
            state == PlaySession::State::Stopped ? startPlay(frame) : stopPlay(frame);
        if (ImGui::MenuItem(state == PlaySession::State::Paused ? "Resume" : "Pause", "Ctrl+Shift+P", false, m_play.active()))
            state == PlaySession::State::Paused ? m_play.play() : m_play.pause();
        if (ImGui::MenuItem("Step", "Ctrl+Alt+P", false, state == PlaySession::State::Paused))
            m_play.step();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        for (const auto& panel : m_panels)
        {
            const std::string name{ panel->name() };
            if (ImGui::MenuItem(name.c_str(), nullptr, panel->isOpen()))
                panel->setOpen(!panel->isOpen());
        }
        if (ImGui::MenuItem("Viewport", nullptr, m_viewport.isOpen()))
            m_viewport.setOpen(!m_viewport.isOpen());
        ImGui::EndMenu();
    }

    drawPlayControls(frame);
    ImGui::EndMainMenuBar();
}

void EditorShell::drawPlayControls(FrameContext& frame)
{
    const auto state = m_play.state();
    // Their own IDs: the Play menu has the same label
    const char* play = state == PlaySession::State::Stopped ? "Play##Button" : "Stop##Button";
    const char* pause = state == PlaySession::State::Paused ? "Resume##Button" : "Pause##Button";
    const auto& style = ImGui::GetStyle();
    const float width = ImGui::CalcTextSize(play, nullptr, true).x + ImGui::CalcTextSize("Resume").x + ImGui::CalcTextSize("Step").x + 6.0f * style.FramePadding.x +
                        2.0f * style.ItemSpacing.x;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), (ImGui::GetWindowWidth() - width) * 0.5f));
    if (ImGui::SmallButton(play))
        state == PlaySession::State::Stopped ? startPlay(frame) : stopPlay(frame);
    ImGui::BeginDisabled(!m_play.active());
    if (ImGui::SmallButton(pause))
        state == PlaySession::State::Paused ? m_play.play() : m_play.pause();
    ImGui::EndDisabled();
    ImGui::BeginDisabled(state != PlaySession::State::Paused);
    if (ImGui::SmallButton("Step##Button"))
        m_play.step();
    ImGui::EndDisabled();
}
}
