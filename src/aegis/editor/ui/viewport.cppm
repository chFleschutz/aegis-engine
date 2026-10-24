export module aegis.editor:viewport;
import std;
import aegis.editor.core;
import aegis.engine;
import aegis.reflect;
import aegis.world;

export namespace aegis::editor
{
/// @brief The scene view: the world renders into a texture of the panel's size, through the editor camera, which
///        the viewport moves (RMB + WASD/QE fly, Alt + LMB orbit, MMB pan, wheel dolly, F focus). A click
///        selects the mesh or the light or camera icon under the cursor; the gizmo moves the selection
///        (W/E/R/Q while the viewport has focus). EditorShell owns it.
class Viewport
{
public:
    /// @brief Draws the panel and applies this frame's camera moves, gizmo edits and clicks.
    void draw(EditorContext& editor, FrameContext& frame);

    /// @brief The view the world renders through: the editor camera (in the game view, the world's main camera) at the
    ///        settled panel size; none while closed.
    [[nodiscard]] auto renderView() const -> std::optional<RenderView>;
    /// @brief Whether the viewport takes the mouse and keyboard: it is hovered, or the camera is being dragged.
    [[nodiscard]] auto capturesInput() const -> bool { return m_hovered || cameraDragged(); }
    /// @brief Whether the game gets the input: the game view has focus.
    [[nodiscard]] auto givesInputToGame() const -> bool { return m_gameView && m_focused; }

    /// @brief Shows the world through its main camera (while playing) instead of the editor camera, which then stays
    ///        where it is; there is no gizmo, icon or picking in it.
    void setGameView(bool gameView);

    /// @brief Places the editor camera at the world's main camera again in the next frame (another scene was opened).
    void placeCameraAgain() { m_cameraPlaced = false; }

    [[nodiscard]] auto isOpen() const -> bool { return m_open; }
    void setOpen(bool open) { m_open = open; }

private:
    enum class GizmoMode
    {
        None,
        Translate,
        Rotate,
        Scale,
    };

    enum class GizmoSpace
    {
        Local,
        World,
    };

    // The panel's image in screen pixels.
    struct Rect
    {
        float x{ 0.0f };
        float y{ 0.0f };
        float width{ 1.0f };
        float height{ 1.0f };
    };

    [[nodiscard]] auto cameraDragged() const -> bool { return m_flying || m_orbiting || m_panning; }
    [[nodiscard]] auto aspect() const -> float;
    void placeCamera(world::World& world);
    void updateCamera(EditorContext& editor, float deltaSeconds);
    void focusSelection(const EditorContext& editor);
    void updateGizmoMode();
    // Whether a gizmo was drawn: the selection can be moved.
    auto drawGizmo(EditorContext& editor, const reflect::TypeRegistry& types, const Rect& rect) -> bool;
    void drawToolbar(EditorContext& editor, const Rect& rect);
    [[nodiscard]] auto drawIcons(const EditorContext& editor, const Rect& rect) -> std::vector<PickIcon>;
    void pick(EditorContext& editor, const Rect& rect, std::span<const PickIcon> icons, bool gizmo);

    EditorCamera m_camera;
    SettledExtent m_extent;
    GizmoMode m_gizmoMode{ GizmoMode::Translate };
    GizmoSpace m_gizmoSpace{ GizmoSpace::Local };
    bool m_snap{ false }; // Ctrl inverts it
    bool m_open{ true };
    bool m_gameView{ false };
    bool m_cameraPlaced{ false };
    bool m_hovered{ false };
    bool m_focused{ false };
    bool m_flying{ false };
    bool m_orbiting{ false };
    bool m_panning{ false };
    bool m_gizmoDragging{ false }; // As of the last frame: its end seals the drag's undo entry
};
}
