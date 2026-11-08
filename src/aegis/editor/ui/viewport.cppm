export module aegis.editor:viewport;
import std;
import aegis.editor.core;
import aegis.engine;
import aegis.math;
import aegis.reflect;
import aegis.world;

export namespace aegis::editor
{
/// @brief The scene view: the world renders into a texture of the panel's size, through the editor camera, which
///        the viewport moves (RMB + WASD/QE fly, Alt + LMB orbit, MMB pan, wheel dolly; F, the focus action, frames the selection). A click
///        selects the mesh or the light or camera icon under the cursor, Ctrl+click adds it to the selection or removes it; the gizmo
///        sits on the active entity and moves every selected root with it (W/E/R/Q while the viewport has focus). Corner brackets mark
///        what a click picks by (a mesh's bounding sphere, else an icon): around the selected entities, strongest on the active one, and dim
///        around the one under the cursor. A scene or model dragged from the asset browser is instantiated on the ground under the cursor.
///        EditorShell owns it.
class Viewport
{
public:
    /// @brief Draws the panel and applies this frame's camera moves, gizmo edits and clicks. With 'bar', a row above the
    ///        image holds what it draws (the recovery bar's text and buttons), and the image is that much shorter.
    void draw(EditorContext& editor, FrameContext& frame, const std::function<void()>& bar = {});

    /// @brief The view the world renders through: the editor camera (in the game view, the world's main camera) at the
    ///        settled panel size; none until the panel has a size.
    [[nodiscard]] auto renderView() const -> std::optional<RenderView>;
    /// @brief Whether the viewport takes the mouse and keyboard: it is hovered, or the camera is being dragged.
    [[nodiscard]] auto capturesInput() const -> bool { return m_hovered || cameraDragged(); }
    /// @brief Whether the game gets the input: the game view has focus.
    [[nodiscard]] auto givesInputToGame() const -> bool { return m_gameView && m_focused; }
    /// @brief Takes the input back from the game: the viewport loses its focus until it is clicked again.
    void releaseInput();
    /// @brief Records the gizmo drag still going, if any, as one undo entry: when the gizmo is released, or before the
    ///        world the panels show is switched (play).
    void endGizmoDrag(EditorContext& editor, FrameContext& frame);
    /// @brief What the viewport shows in a corner while the game has the input: how to take it back.
    void setReleaseHint(std::string hint) { m_releaseHint = std::move(hint); }

    /// @brief Shows the world through its main camera (while playing) instead of the editor camera, which then stays
    ///        where it is; there is no gizmo, icon or picking in it.
    void setGameView(bool gameView);

    /// @brief Places the camera at the world's main camera again next frame (a scene was opened).
    void resetCamera() { m_cameraPlaced = false; }

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
    // Whether snapping is in effect: the snap button's state, inverted while Ctrl is held.
    [[nodiscard]] auto snapping() const -> bool;
    void placeCamera(world::World& world);
    void updateCamera(EditorContext& editor, float deltaSeconds);
    void focusSelection(const EditorContext& editor);
    void updateGizmoMode();
    // Whether a gizmo was drawn: the selection can be moved.
    auto drawGizmo(EditorContext& editor, FrameContext& frame, const Rect& rect) -> bool;
    // A gizmo drag starts with the active entity's world matrix 'start': it keeps the selected roots' as they are now.
    void beginGizmoDrag(EditorContext& editor, const reflect::TypeInfo& transformType, const glm::mat4& start);
    void drawToolbar(EditorContext& editor, const Rect& rect);
    // A row across the panel at the cursor, on its own background, holding what 'bar' draws; the cursor ends below it.
    static void drawBar(const std::function<void()>& bar);
    // 'text' in the bottom left corner: how to take the input back from the game, the camera speed while it changes.
    void drawCornerLabel(const Rect& rect, const std::string& text) const;
    [[nodiscard]] auto drawIcons(const EditorContext& editor, const Rect& rect) -> std::vector<PickIcon>;
    // Whether a left click now would pick: the cursor is on the image, not on this frame's gizmo or the toolbar, and Alt (orbit) is up.
    [[nodiscard]] auto canPick(bool gizmo) const -> bool;
    // The entity a click at 'cursor' picks: an icon there, else the nearest mesh along the ray.
    [[nodiscard]] auto pickAt(const EditorContext& editor, const Rect& rect, std::span<const PickIcon> icons, const glm::vec2& cursor) const -> std::optional<StableId>;
    void pick(EditorContext& editor, const Rect& rect, std::span<const PickIcon> icons, bool gizmo);
    // Picks again what is under the cursor when it moved with no button held.
    void updateHover(const EditorContext& editor, const Rect& rect, std::span<const PickIcon> icons, bool gizmo);
    // The brackets around the selected entities and the hovered one.
    void drawBrackets(const EditorContext& editor, const Rect& rect, std::span<const PickIcon> icons) const;
    // A scene or model dropped from the asset browser: instantiated where the cursor points.
    void acceptDrop(EditorContext& editor, FrameContext& frame, const Rect& rect);

    EditorCamera m_camera;
    SettledExtent m_extent;
    GizmoMode m_gizmoMode{ GizmoMode::Translate };
    GizmoSpace m_gizmoSpace{ GizmoSpace::Local };
    bool m_snap{ false }; // Ctrl inverts it
    bool m_gameView{ false };
    bool m_cameraPlaced{ false };
    bool m_hovered{ false };
    bool m_focused{ false };
    bool m_flying{ false };
    bool m_orbiting{ false };
    bool m_panning{ false };
    // What a click would pick, from the last time the cursor moved; picked again when it moves while 'm_hoverCurrent' is false
    std::optional<StableId> m_hoveredId;
    bool m_hoverCurrent{ false };
    // A drag of the gizmo: it sits on the active entity and moves every selected root (selectedRoots) with it.
    struct GizmoRoot
    {
        StableId id;
        glm::mat4 start{ 1.0f }; // Its world matrix when the drag began
        FieldValues oldValues;   // Its Transform then
    };
    struct GizmoDrag
    {
        glm::mat4 start{ 1.0f }; // The active entity's world matrix when it began
        std::vector<GizmoRoot> roots;
        std::string verb; // Of its undo entry: Move, Rotate or Scale
    };
    std::optional<GizmoDrag> m_gizmoDrag; // Recorded when the gizmo is released
    double m_speedShownUntil{ 0.0 };      // ImGui's time until which the camera speed shows while flying
    std::string m_releaseHint;
};
}
