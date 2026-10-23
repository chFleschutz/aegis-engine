export module aegis.editor.core:editor_camera;
import std;
import aegis.math;
import :picking;

export namespace aegis::editor
{
/// @brief The input the editor camera follows this frame, gathered by the viewport. Each mode moves the camera
///        only while its flag is set; the wheel dollies, or changes the speed while flying.
struct CameraInput
{
    /// @brief Cursor movement in pixels, x to the right and y down.
    glm::vec2 cursorDelta{ 0.0f };
    /// @brief Wheel steps, positive away from the user.
    float wheel{ 0.0f };
    /// @brief Fly direction in camera space, each axis -1 to 1: x right (D/A), y forward (W/S), z up (E/Q).
    glm::vec3 move{ 0.0f };
    /// @brief RMB held: look around and move.
    bool fly{ false };
    /// @brief Alt + LMB: turn around the pivot.
    bool orbit{ false };
    /// @brief MMB: move the camera and the pivot in the view plane.
    bool pan{ false };
};

/// @brief The editor's camera. It belongs to the editor, never to a world, so it can't end up in a scene file
///        or be moved by a script. It looks at a pivot `distance()` ahead, which orbiting turns around and
///        dollying approaches. Z is up; yaw 0 looks along +Y.
class EditorCamera
{
public:
    /// @brief Radians the camera turns per pixel of cursor movement.
    static constexpr float turnRate{ 0.004f };
    /// @brief The closest the camera dollies to its pivot.
    static constexpr float minDistance{ 0.05f };

    EditorCamera();

    /// @brief Moves the camera to 'position', looking along 'forward', with the pivot 'distance' ahead.
    void lookAlong(const glm::vec3& position, const glm::vec3& forward, float distance = 10.0f);
    /// @brief Moves the camera back along its view direction until the sphere fills about the view.
    void focus(const glm::vec3& center, float radius);
    /// @brief Applies a frame of input. Flying moves 'speed()' units per second, faster with the wheel.
    void update(const CameraInput& input, float deltaSeconds);

    [[nodiscard]] auto position() const -> glm::vec3 { return m_position; }
    [[nodiscard]] auto pivot() const -> glm::vec3 { return m_position + forward() * m_distance; }
    [[nodiscard]] auto distance() const -> float { return m_distance; }
    [[nodiscard]] auto yaw() const -> float { return m_yaw; }
    [[nodiscard]] auto pitch() const -> float { return m_pitch; }
    [[nodiscard]] auto forward() const -> glm::vec3;
    [[nodiscard]] auto right() const -> glm::vec3;
    [[nodiscard]] auto up() const -> glm::vec3;

    /// @brief Units per second while flying; the wheel scales it while flying.
    [[nodiscard]] auto speed() const -> float { return m_speed; }
    void setSpeed(float speed) { m_speed = speed; }

    /// @brief Vertical field of view in radians.
    float fov{ glm::radians(60.0f) };
    float near{ 0.1f };
    float far{ 1000.0f };

    /// @brief The world-to-view matrix (right handed, looking along -Z in view space).
    [[nodiscard]] auto view() const -> glm::mat4;
    /// @brief The perspective projection with depth range [0, 1] and Vulkan's flipped y, as the renderer uses.
    [[nodiscard]] auto projection(float aspect) const -> glm::mat4;
    /// @brief The ray through 'ndc' (-1 to 1, y up) of a view with 'aspect' (width / height).
    [[nodiscard]] auto ray(const glm::vec2& ndc, float aspect) const -> Ray;
    /// @brief Where 'point' lands in the view, in ndc (y up); none behind the camera.
    [[nodiscard]] auto project(const glm::vec3& point, float aspect) const -> std::optional<glm::vec2>;

private:
    glm::vec3 m_position{ 0.0f };
    float m_yaw{ 0.0f };
    float m_pitch{ 0.0f };
    float m_distance{ 10.0f };
    float m_speed{ 5.0f };
};
}
