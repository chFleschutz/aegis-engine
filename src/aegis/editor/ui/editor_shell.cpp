module;

#include <imgui.h>
#include <ImGuizmo.h>

module aegis.editor;
import std;
import aegis.core;
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
    const bool viewport = m_viewport.capturesInput();
    return { .keyboard = io.WantCaptureKeyboard || viewport, .mouse = io.WantCaptureMouse || viewport };
}

void EditorShell::onFrame(FrameContext& frame)
{
    m_context.setWorld(frame.world);
    m_context.selection().prune(frame.world.registry());

    ImGui::GetStyle().FontScaleMain = m_context.preferences().uiScale;
    ImGuizmo::BeginFrame();

    beginMainWindow();
    drawMenuBar();
    for (const auto& panel : m_panels)
    {
        if (panel->isOpen())
            panel->draw(m_context, frame);
    }
    m_viewport.draw(m_context, frame);
    drawStatusBar(frame);
    ImGui::End();

    // Not while a value is being dragged: that would write the file every frame
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        savePreferencesIfChanged();
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

void EditorShell::drawMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
        return;

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

    ImGui::EndMainMenuBar();
}
}
