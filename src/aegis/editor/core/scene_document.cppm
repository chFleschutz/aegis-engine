export module aegis.editor.core:scene_document;
import std;
import aegis.reflect;
import :context;
import :error;

export namespace aegis::editor
{
/// @brief What the scene document asks the shell to do to the edit world (SceneDocument::takeAction), and what the
///        shell reports back once it did.
struct SceneAction
{
    enum class Kind
    {
        New,     // Empty the world for a new scene; then opened(none)
        Open,    // Load the scene file 'scene'; then opened(scene), or opened(none) when it fails to load
        Quit,    // Close the editor
        Restore, // Replace the world with the autosave 'autosave'; then restored(), or restoreFailed()
    };
    Kind kind;
    std::uint64_t scene{ 0 };       // Open's asset ID
    std::filesystem::path autosave; // Restore's file

    [[nodiscard]] auto operator==(const SceneAction&) const -> bool = default;
};

/// @brief An autosave newer than its scene's file, which the recovery bar offers.
struct Recovery
{
    std::filesystem::path file;
    std::filesystem::file_time_type written;
};

/// @brief The scene the editor edits, as a document: which scene file it is (the one owner of the current scene), whether
///        it has unsaved changes (the edit history's saved marker), the action waiting for them to be saved or
///        discarded, the autosave timing and the recovery of an autosave. It decides and reports; the shell draws the
///        prompts and performs what it asks for (takeAction), writes the scene files and tells it the outcome. It
///        writes, finds and deletes the autosaves itself, in its autosave folder.
class SceneDocument
{
public:
    /// @brief What the document waits for before its pending action can run, or a save can go on.
    enum class Wait
    {
        None,
        Prompt, // The unsaved-changes prompt: save(), discard() or cancel()
        SaveAs, // A name for the scene's file: savedAs() or cancel()
    };

    explicit SceneDocument(EditorContext& context) :
        m_context{ context }
    {
    }

    /// @brief Where the autosaves go (autosaveFolder of the project); empty keeps none (headless, no project).
    void setAutosaveFolder(std::filesystem::path folder) { m_autosaveFolder = std::move(folder); }
    [[nodiscard]] auto autosaveFolder() const -> const std::filesystem::path& { return m_autosaveFolder; }

    /// @brief The scene file the edit world holds (its asset ID); none for a new scene.
    [[nodiscard]] auto scene() const -> std::optional<std::uint64_t> { return m_scene; }
    /// @brief Whether the edit world has changes that are not saved (EditorContext::unsaved).
    [[nodiscard]] auto unsaved() const -> bool { return m_context.unsaved(); }

    /// @brief The edit world now holds 'scene' as its file has it, or a new scene (none): at startup, the scene the
    ///        launcher loaded, and after New, Open or a scene that failed to load. Forgets the selection, the history,
    ///        the autosave timing and the recovery offer (offerRecovery offers the new scene's).
    void opened(std::optional<std::uint64_t> scene);
    /// @brief Restore replaced the edit world with the autosave: the scene keeps its file and ID, so Save writes the
    ///        restored changes there; its history is empty and it is unsaved.
    void restored();
    /// @brief Restore created nothing: takeAction then reopens the scene as its file has it (or a new one).
    void restoreFailed();

    /// @brief Asks to run 'action' (New, Open, Quit): with unsaved changes it waits for the prompt, otherwise
    ///        takeAction returns it.
    void request(SceneAction action);
    /// @brief Whether the window may close now; with unsaved changes it asks as request(Quit) does, unless the
    ///        document waits already (the prompt, or Save As with or without an action).
    [[nodiscard]] auto canClose() -> bool;
    [[nodiscard]] auto waiting() const -> Wait { return m_wait; }
    /// @brief The action waiting for the unsaved changes to be saved or discarded; none if nothing waits.
    [[nodiscard]] auto pending() const -> const std::optional<SceneAction>& { return m_pending; }
    /// @brief The action to perform now, once; none if nothing is ready.
    [[nodiscard]] auto takeAction() -> std::optional<SceneAction> { return std::exchange(m_ready, std::nullopt); }

    /// @brief Save (File > Save, the prompt's Save): whether to write the scene to its file now, then saved() or
    ///        saveFailed(). A scene without a file ('hasFile', none in the asset database) waits for Save As instead,
    ///        keeping the pending action.
    [[nodiscard]] auto save(bool hasFile) -> bool;
    /// @brief Save As (File > Save As): waits for a name.
    void requestSaveAs() { m_wait = Wait::SaveAs; }
    /// @brief The scene was written to its file: it is saved, its autosave is deleted, and the pending action runs.
    void saved();
    /// @brief The scene was written to the new file 'scene' (its asset ID), which is the current scene from now on:
    ///        it is saved, the old scene's autosave is deleted, and the pending action runs.
    void savedAs(std::uint64_t scene);
    /// @brief The write failed (the shell reports why): nothing is discarded, so the pending action is dropped.
    void saveFailed();
    /// @brief The prompt's Don't Save: deletes the scene's autosave, and the pending action runs.
    void discard();
    /// @brief The prompt's or Save As's Cancel: the pending action is dropped.
    void cancel();

    /// @brief The current scene's autosave (autosavePath); empty without an autosave folder.
    [[nodiscard]] auto autosaveFile() const -> std::filesystem::path;
    /// @brief Once per frame: hides the recovery offer once the scene has unsaved changes (the autosave stays until the
    ///        next one replaces it), and tells whether an autosave is due: the scene has had unsaved changes for the
    ///        preference's minutes since they began or since the last autosave.
    [[nodiscard]] auto update(std::chrono::steady_clock::time_point now) -> bool;
    /// @brief Writes the edit world to the scene's autosave (unless autosaving is off or there is no folder); the next
    ///        one is due the preference's minutes after 'now', also after a failure.
    [[nodiscard]] auto autosave(const reflect::TypeRegistry& types, std::chrono::steady_clock::time_point now)
        -> std::expected<void, Error>;

    /// @brief Offers the scene's autosave when it is newer than 'sceneFile' (empty for a new scene); hides the offer
    ///        otherwise.
    void offerRecovery(const std::filesystem::path& sceneFile);
    /// @brief The autosave offered; none while the bar is hidden.
    [[nodiscard]] auto recovery() const -> const std::optional<Recovery>& { return m_recovery; }
    /// @brief The bar's Restore: takeAction returns the Restore of the offered autosave.
    void restoreRecovery();
    /// @brief The bar's Discard: deletes the offered autosave.
    void discardRecovery();

private:
    /// @brief Deletes the autosave of 'scene' (its changes were saved or discarded), and the offer of it.
    void deleteAutosave(std::optional<std::uint64_t> scene);
    /// @brief Ends the wait; the pending action is ready to run.
    void runPending();

    EditorContext& m_context;
    std::filesystem::path m_autosaveFolder;
    std::optional<std::uint64_t> m_scene;
    std::optional<SceneAction> m_pending;
    std::optional<SceneAction> m_ready;
    Wait m_wait{ Wait::None };
    std::optional<std::chrono::steady_clock::time_point> m_autosaveFrom; // When the unsaved changes began, or the last autosave
    std::optional<Recovery> m_recovery;
};
}
