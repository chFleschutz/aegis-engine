module;

#include <imgui.h>
#include <ImGuizmo.h>

module aegis.editor;
import std;
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
    applyStyle();
    // A drag in a window's body is the viewport's (orbit, gizmo), not a window move
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
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
    drawStatusBar(frame);
    ImGui::End();
    ImGui::PopStyleColor(tint);
    updateWorlds(frame);

    // Not while a value is being dragged: that would write the file every frame
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        savePreferencesIfChanged();
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
