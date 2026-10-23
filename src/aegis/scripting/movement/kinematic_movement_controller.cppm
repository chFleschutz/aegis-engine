export module aegis.scripting.movement.kinematic_movement_controller;

import std;
import aegis.reflect;
import aegis.scripting.script;
import aegis.math;
import aegis.platform;
import aegis.world.components;

export namespace aegis::scripting
{
/// @brief Moves its entity with the keyboard and looks around with the mouse (a fly camera)
/// @note Registered as "KinematicMovementController" (registerKinematicMovementController), its speeds are fields
class KinematicMovementController : public Script
{
public:
    static constexpr float DEFAULT_MOVE_SPEED = 5.0f;

    /// @brief Default keybindings for movement and view control
    struct KeyMappings
    {
        aegis::platform::Key moveLeft = aegis::platform::Key::A;
        aegis::platform::Key moveRight = aegis::platform::Key::D;
        aegis::platform::Key moveForward = aegis::platform::Key::W;
        aegis::platform::Key moveBackward = aegis::platform::Key::S;
        aegis::platform::Key moveUp = aegis::platform::Key::E;
        aegis::platform::Key moveDown = aegis::platform::Key::Q;

        // Speed modifiers
        aegis::platform::Key increaseSpeed = aegis::platform::Key::PageUp;
        aegis::platform::Key decreaseSpeed = aegis::platform::Key::PageDown;
        aegis::platform::Key resetSpeed = aegis::platform::Key::Home;
        aegis::platform::Key speedupModifier = aegis::platform::Key::LeftShift;
        aegis::platform::Key slowdownModifier = aegis::platform::Key::LeftControl;

        aegis::platform::MouseButton mouseRotate = aegis::platform::MouseButton::Right;
        aegis::platform::MouseButton mousePan = aegis::platform::MouseButton::Middle;

        // Disabled by default
        aegis::platform::Key lookLeft = aegis::platform::Key::Unknown;
        aegis::platform::Key lookRight = aegis::platform::Key::Unknown;
        aegis::platform::Key lookUp = aegis::platform::Key::Unknown;
        aegis::platform::Key lookDown = aegis::platform::Key::Unknown;
    };

    // The persistent state, registered as fields
    float moveSpeed{ DEFAULT_MOVE_SPEED };
    float lookSpeed{ 1.5f };
    float mouseSensitivity{ 0.004f }; // Radians per pixel
    float mousePanSpeed{ 0.02f };     // World units per pixel

    void onUpdate(ScriptContext& context, float deltaSeconds) override;

    void onDestroy(ScriptContext& context) override;

private:
    [[nodiscard]] static auto cursorDelta(const platform::InputState& input) -> glm::vec2;

    void applyRotation(ScriptContext& context, float deltaSeconds);

    void applyMovement(ScriptContext& context, float deltaSeconds);

    void toggleMouseRotate(ScriptContext& context, bool enabled);

    void toogleMousePan(ScriptContext& context, bool enabled);

    KeyMappings m_keys{};
    bool m_mouseRotateEnabled{ false };
    bool m_mousePanEnabled{ false };
};
}

export namespace aegis::scripting
{
/// @brief Registers KinematicMovementController and its speeds with the type registry
auto registerKinematicMovementController(reflect::TypeRegistry& types) -> std::expected<void, reflect::Error>;
}
