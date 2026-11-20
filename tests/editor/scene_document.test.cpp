#include <doctest/doctest.h>

import std;
import aegis.editor.core;
import aegis.reflect;
import aegis.world;

namespace
{
using aegis::editor::EditorContext;
using aegis::editor::SceneAction;
using aegis::editor::SceneDocument;
using aegis::reflect::TypeRegistry;
using aegis::world::World;
using Wait = SceneDocument::Wait;
using namespace std::chrono_literals;

// A fresh folder under the temp directory, removed afterwards.
class TempFolder
{
public:
    TempFolder() :
        m_path{ std::filesystem::temp_directory_path() / std::format("aegis-document-{}", std::chrono::steady_clock::now().time_since_epoch().count()) }
    {
        std::filesystem::create_directories(m_path);
    }
    TempFolder(const TempFolder&) = delete;
    auto operator=(const TempFolder&) -> TempFolder& = delete;
    ~TempFolder()
    {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    [[nodiscard]] auto path() const -> const std::filesystem::path& { return m_path; }

private:
    std::filesystem::path m_path;
};

void touch(const std::filesystem::path& file)
{
    std::filesystem::create_directories(file.parent_path());
    std::ofstream{ file } << "{}";
}

// An editor on a world, with the scene file 'scene' open (none: a new scene) and autosaves in a folder of its own.
struct Editor
{
    explicit Editor(std::optional<std::uint64_t> scene = 42)
    {
        context.setWorld(world);
        document.setAutosaveFolder(folder.path() / "autosave");
        document.opened(scene);
    }

    // An edit: the scene has unsaved changes from here on.
    void edit() { context.undoStack().markUnsaved(); }

    TempFolder folder;
    World world;
    EditorContext context;
    SceneDocument document{ context };
};

const SceneAction open7{ .kind = SceneAction::Kind::Open, .scene = 7 };
const SceneAction quit{ .kind = SceneAction::Kind::Quit };
} // namespace

TEST_SUITE("editor::SceneDocument")
{
    TEST_CASE("runs an action at once without unsaved changes")
    {
        Editor editor;
        editor.document.request(open7);

        CHECK(editor.document.waiting() == Wait::None);
        CHECK(editor.document.takeAction() == open7);
        CHECK_FALSE(editor.document.takeAction()); // Once
    }

    TEST_CASE("opening the scene the shell loaded makes it current and forgets the history")
    {
        Editor editor;
        editor.edit();
        editor.document.opened(7);

        CHECK(editor.document.scene() == 7u);
        CHECK_FALSE(editor.document.unsaved());
        editor.document.opened(std::nullopt);
        CHECK_FALSE(editor.document.scene());
    }

    TEST_CASE("open with unsaved changes asks first")
    {
        Editor editor;
        editor.edit();
        editor.document.request(open7);

        CHECK(editor.document.waiting() == Wait::Prompt);
        CHECK(editor.document.pending() == open7);
        CHECK_FALSE(editor.document.takeAction());

        SUBCASE("Save writes the file, then opens")
        {
            REQUIRE(editor.document.save(true));
            editor.document.saved();
            CHECK_FALSE(editor.document.unsaved());
            CHECK(editor.document.waiting() == Wait::None);
            CHECK(editor.document.takeAction() == open7);
        }
        SUBCASE("Save of a scene without a file asks for a name, then opens")
        {
            CHECK_FALSE(editor.document.save(false));
            CHECK(editor.document.waiting() == Wait::SaveAs);
            CHECK(editor.document.pending() == open7);
            editor.document.savedAs(9);
            CHECK(editor.document.takeAction() == open7);
        }
        SUBCASE("a failed save discards nothing and drops the open")
        {
            REQUIRE(editor.document.save(true));
            editor.document.saveFailed();
            CHECK(editor.document.unsaved());
            CHECK(editor.document.waiting() == Wait::None);
            CHECK_FALSE(editor.document.pending());
            CHECK_FALSE(editor.document.takeAction());
        }
        SUBCASE("Don't Save opens without saving")
        {
            editor.document.discard();
            CHECK(editor.document.unsaved()); // Until the shell opened the other scene
            CHECK(editor.document.takeAction() == open7);
        }
        SUBCASE("Cancel keeps the scene and drops the open")
        {
            editor.document.cancel();
            CHECK(editor.document.waiting() == Wait::None);
            CHECK(editor.document.unsaved());
            CHECK_FALSE(editor.document.takeAction());
        }
        SUBCASE("cancelling the name Save asked for drops the open")
        {
            CHECK_FALSE(editor.document.save(false));
            editor.document.cancel();
            CHECK_FALSE(editor.document.takeAction());
            CHECK(editor.document.unsaved());
        }
    }

    TEST_CASE("Save As makes the new file the current scene")
    {
        Editor editor;
        editor.edit();
        const auto before = editor.document.autosaveFile();
        touch(before);

        editor.document.requestSaveAs();
        CHECK(editor.document.waiting() == Wait::SaveAs);
        editor.document.savedAs(9);

        CHECK(editor.document.scene() == 9u);
        CHECK_FALSE(editor.document.unsaved());
        CHECK(editor.document.waiting() == Wait::None);
        CHECK_FALSE(std::filesystem::exists(before)); // The old scene's autosave held the changes it saved
        CHECK(editor.document.autosaveFile() != before);
        CHECK_FALSE(editor.document.takeAction());
    }

    TEST_CASE("Save deletes the scene's autosave")
    {
        Editor editor;
        editor.edit();
        touch(editor.document.autosaveFile());
        REQUIRE(editor.document.save(true));
        editor.document.saved();

        CHECK_FALSE(std::filesystem::exists(editor.document.autosaveFile()));
    }

    TEST_CASE("closing while dirty asks first and quits once answered")
    {
        Editor editor;
        CHECK(editor.document.canClose());

        editor.edit();
        CHECK_FALSE(editor.document.canClose());
        CHECK(editor.document.waiting() == Wait::Prompt);
        CHECK(editor.document.pending() == quit);

        SUBCASE("asking again while the close waits keeps it")
        {
            CHECK_FALSE(editor.document.canClose());
            CHECK(editor.document.pending() == quit);
        }
        SUBCASE("an action that waits already is not replaced by the close")
        {
            editor.document.cancel();
            editor.document.request(open7);
            CHECK_FALSE(editor.document.canClose());
            CHECK(editor.document.pending() == open7);
        }
        SUBCASE("Don't Save quits")
        {
            editor.document.discard();
            CHECK(editor.document.takeAction() == quit);
        }
    }

    TEST_CASE("an autosave is due after the preference's minutes of unsaved changes")
    {
        Editor editor;
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());
        editor.context.preferences().autosaveMinutes = 5;
        const auto start = std::chrono::steady_clock::time_point{} + 1h;

        CHECK_FALSE(editor.document.update(start + 10min)); // Saved: never due

        editor.edit();
        CHECK_FALSE(editor.document.update(start)); // The changes begin
        CHECK_FALSE(editor.document.update(start + 4min));
        REQUIRE(editor.document.update(start + 5min));

        REQUIRE(editor.document.autosave(types, start + 5min).has_value());
        CHECK(std::filesystem::exists(editor.document.autosaveFile()));
        CHECK_FALSE(editor.document.update(start + 9min)); // Five minutes from the last autosave
        CHECK(editor.document.update(start + 10min));

        SUBCASE("not with autosaving off")
        {
            editor.context.preferences().autosaveMinutes = 0;
            CHECK_FALSE(editor.document.update(start + 60min));
        }
        SUBCASE("not without an autosave folder")
        {
            editor.document.setAutosaveFolder({});
            CHECK_FALSE(editor.document.update(start + 60min));
        }
    }

    TEST_CASE("offers an autosave newer than the scene file and restores it")
    {
        Editor editor;
        const auto sceneFile = editor.folder.path() / "content" / "scene.scene";
        touch(sceneFile);
        std::filesystem::last_write_time(sceneFile, std::filesystem::file_time_type::clock::now() - 1h);
        const auto autosave = editor.document.autosaveFile();
        touch(autosave);

        editor.document.offerRecovery(sceneFile);
        REQUIRE(editor.document.recovery());
        CHECK(editor.document.recovery()->file == autosave);

        SUBCASE("Restore asks the shell to restore it, and the scene keeps its file, unsaved")
        {
            editor.document.restoreRecovery();
            CHECK_FALSE(editor.document.recovery());
            CHECK(editor.document.takeAction() == SceneAction{ .kind = SceneAction::Kind::Restore, .autosave = autosave });
            editor.document.restored();
            CHECK(editor.document.scene() == 42u);
            CHECK(editor.document.unsaved());
            CHECK_FALSE(editor.context.undoStack().canUndo());
        }
        SUBCASE("a failed restore reopens the scene")
        {
            editor.document.restoreRecovery();
            CHECK(editor.document.takeAction()->kind == SceneAction::Kind::Restore);
            editor.document.restoreFailed();
            CHECK(editor.document.takeAction() == SceneAction{ .kind = SceneAction::Kind::Open, .scene = 42 });
        }
        SUBCASE("Discard deletes it")
        {
            editor.document.discardRecovery();
            CHECK_FALSE(editor.document.recovery());
            CHECK_FALSE(std::filesystem::exists(autosave));
        }
        SUBCASE("an edit hides the offer and keeps the autosave")
        {
            editor.edit();
            CHECK_FALSE(editor.document.update(std::chrono::steady_clock::now()));
            CHECK_FALSE(editor.document.recovery());
            CHECK(std::filesystem::exists(autosave));
        }
        SUBCASE("opening another scene hides the offer")
        {
            editor.document.opened(7);
            CHECK_FALSE(editor.document.recovery());
        }
    }

    TEST_CASE("offers no autosave older than the scene file")
    {
        Editor editor;
        const auto autosave = editor.document.autosaveFile();
        touch(autosave);
        std::filesystem::last_write_time(autosave, std::filesystem::file_time_type::clock::now() - 1h);
        const auto sceneFile = editor.folder.path() / "content" / "scene.scene";
        touch(sceneFile);

        editor.document.offerRecovery(sceneFile);
        CHECK_FALSE(editor.document.recovery());
    }

    TEST_CASE("a failed restore of a new scene starts a new one")
    {
        Editor editor{ std::nullopt };
        touch(editor.document.autosaveFile());
        editor.document.offerRecovery({});
        REQUIRE(editor.document.recovery());

        editor.document.restoreRecovery();
        CHECK(editor.document.takeAction()->kind == SceneAction::Kind::Restore);
        editor.document.restoreFailed();
        CHECK(editor.document.takeAction() == SceneAction{ .kind = SceneAction::Kind::New });
    }
}
