module aegis.editor.core;
import std;
import aegis.core;
import aegis.reflect;
import aegis.world;

namespace aegis::editor
{
void SceneDocument::opened(std::optional<std::uint64_t> scene)
{
    m_scene = scene;
    m_context.setScene(scene);
    m_context.resetScene();
    m_autosaveFrom.reset();
    m_recovery.reset();
}

void SceneDocument::restored()
{
    m_context.resetScene();
    m_context.undoStack().markUnsaved(); // The saved state is the file, which is not in the history
    m_autosaveFrom.reset();
}

void SceneDocument::restoreFailed()
{
    m_ready = m_scene ? SceneAction{ .kind = SceneAction::Kind::Open, .scene = *m_scene } : SceneAction{ .kind = SceneAction::Kind::New };
}

void SceneDocument::request(SceneAction action)
{
    if (!unsaved())
    {
        m_ready = std::move(action);
        return;
    }
    m_pending = std::move(action);
    m_wait = Wait::Prompt;
}

auto SceneDocument::canClose() -> bool
{
    if (!unsaved())
        return true;
    if (m_wait == Wait::None)
        request({ .kind = SceneAction::Kind::Quit });
    return false;
}

auto SceneDocument::save(bool hasFile) -> bool
{
    if (hasFile)
        return true;
    m_wait = Wait::SaveAs;
    return false;
}

void SceneDocument::saved()
{
    m_context.markSaved();
    deleteAutosave(m_scene);
    runPending();
}

void SceneDocument::savedAs(std::uint64_t scene)
{
    const auto before = std::exchange(m_scene, scene); // Its autosave holds the changes this saved
    m_context.setScene(scene);
    m_context.markSaved();
    deleteAutosave(before);
    runPending();
}

void SceneDocument::saveFailed()
{
    cancel();
}

void SceneDocument::discard()
{
    deleteAutosave(m_scene);
    runPending();
}

void SceneDocument::cancel()
{
    m_pending.reset();
    m_wait = Wait::None;
}

auto SceneDocument::autosaveFile() const -> std::filesystem::path
{
    if (m_autosaveFolder.empty())
        return {};
    return autosavePath(m_autosaveFolder, m_scene);
}

auto SceneDocument::update(std::chrono::steady_clock::time_point now) -> bool
{
    // An edit instead of Restore: the autosave stays until the next one replaces it
    if (m_recovery && unsaved())
        m_recovery.reset();
    if (m_autosaveFolder.empty() || !unsaved())
    {
        m_autosaveFrom.reset();
        return false;
    }
    if (!m_autosaveFrom)
        m_autosaveFrom = now;
    const int minutes = m_context.preferences().autosaveMinutes;
    return minutes > 0 && now - *m_autosaveFrom >= std::chrono::minutes{ minutes };
}

auto SceneDocument::autosave(const reflect::TypeRegistry& types, std::chrono::steady_clock::time_point now) -> std::expected<void, Error>
{
    m_autosaveFrom = now; // Also after a failure: it is retried at the next interval
    auto* world = m_context.editWorld();
    if (m_autosaveFolder.empty() || m_context.preferences().autosaveMinutes == 0 || !world)
        return {};
    return writeAutosave(world->registry(), types, m_scene.value_or(0), autosaveFile());
}

void SceneDocument::offerRecovery(const std::filesystem::path& sceneFile)
{
    m_recovery.reset();
    if (m_autosaveFolder.empty())
        return;
    auto file = autosaveFile();
    if (const auto written = findAutosave(file, sceneFile))
        m_recovery = Recovery{ .file = std::move(file), .written = *written };
}

void SceneDocument::restoreRecovery()
{
    if (m_recovery)
        m_ready = SceneAction{ .kind = SceneAction::Kind::Restore, .autosave = std::exchange(m_recovery, std::nullopt)->file };
}

void SceneDocument::discardRecovery()
{
    if (m_recovery)
        deleteAutosave(m_scene);
}

void SceneDocument::deleteAutosave(std::optional<std::uint64_t> scene)
{
    if (m_autosaveFolder.empty())
        return;
    const auto file = autosavePath(m_autosaveFolder, scene);
    std::error_code error;
    std::filesystem::remove(file, error);
    if (error)
        log::warn("Cannot delete the autosave '{}': {}", file.string(), error.message());
    if (m_recovery && m_recovery->file == file)
        m_recovery.reset();
}

void SceneDocument::runPending()
{
    m_wait = Wait::None;
    if (m_pending)
        m_ready = std::exchange(m_pending, std::nullopt);
}
}
