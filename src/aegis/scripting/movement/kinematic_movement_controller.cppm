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

    void onUpdate(ScriptContext& context, float deltaSeconds) override
    {
        applyRotation(context, deltaSeconds);
        applyMovement(context, deltaSeconds);
    }

    void onDestroy(ScriptContext& context) override
    {
        if (m_mouseRotateEnabled || m_mousePanEnabled) // Give the cursor back
            context.setCursorMode(aegis::platform::CursorMode::Normal);
    }

private:
    [[nodiscard]] static auto cursorDelta(const platform::InputState& input) -> glm::vec2
    {
        const auto [x, y] = input.cursorDelta();
        return { static_cast<float>(x), static_cast<float>(y) };
    }

    void applyRotation(ScriptContext& context, float deltaSeconds)
    {
        auto& transform = context.get<Transform>();
        const auto& input = context.input();

        // Key input rotation (a rate: scaled by the frame time)
        glm::vec3 rotate{ 0.0f };
        if (input.keyDown(m_keys.lookRight))
            rotate.z -= lookSpeed;
        if (input.keyDown(m_keys.lookLeft))
            rotate.z += lookSpeed;
        if (input.keyDown(m_keys.lookUp))
            rotate.x += lookSpeed;
        if (input.keyDown(m_keys.lookDown))
            rotate.x -= lookSpeed;
        rotate *= deltaSeconds;

        // Mouse input rotation (a displacement: independent of the frame time)
        const bool wasRotating = m_mouseRotateEnabled;
        toggleMouseRotate(context, input.mouseDown(m_keys.mouseRotate));
        if (wasRotating && m_mouseRotateEnabled) // The frame the button went down only starts it
        {
            const auto delta = cursorDelta(input);
            rotate.x -= delta.y * mouseSensitivity;
            rotate.z -= delta.x * mouseSensitivity;
        }

        if (glm::length(rotate) > std::numeric_limits<float>::epsilon())
        {
            glm::vec3 rotation = glm::eulerAngles(transform.rotation);
            rotation.x = glm::clamp(rotation.x + rotate.x, -1.5f, 1.5f);
            rotation.z = glm::mod(rotation.z + rotate.z, glm::two_pi<float>());
            rotation.y = 0.0f; // Lock rotation around y-axis
            transform.rotation = glm::quat(rotation);
        }
    }

    void applyMovement(ScriptContext& context, float deltaSeconds)
    {
        auto& transform = context.get<Transform>();
        const auto& input = context.input();
        auto forwardDir = transform.forward();
        auto rightDir = transform.right();
        auto upDir = math::world::UP;

        // Speed modifiers
        if (input.keyDown(m_keys.increaseSpeed))
            moveSpeed += 100.0f * deltaSeconds;
        if (input.keyDown(m_keys.decreaseSpeed))
            moveSpeed -= 100.0f * deltaSeconds;
        if (input.keyDown(m_keys.resetSpeed))
            moveSpeed = DEFAULT_MOVE_SPEED;
        moveSpeed = std::max(moveSpeed, 0.1f);

        auto currentSpeed = moveSpeed;
        if (input.keyDown(m_keys.speedupModifier))
            currentSpeed *= 2.0f;
        if (input.keyDown(m_keys.slowdownModifier))
            currentSpeed *= 0.5f;

        // Key input movement
        glm::vec3 moveDir{ 0.0f };
        if (input.keyDown(m_keys.moveForward))
            moveDir += forwardDir;
        if (input.keyDown(m_keys.moveBackward))
            moveDir -= forwardDir;
        if (input.keyDown(m_keys.moveRight))
            moveDir += rightDir;
        if (input.keyDown(m_keys.moveLeft))
            moveDir -= rightDir;
        if (input.keyDown(m_keys.moveUp))
            moveDir += upDir;
        if (input.keyDown(m_keys.moveDown))
            moveDir -= upDir;

        if (glm::length(moveDir) > std::numeric_limits<float>::epsilon())
        {
            transform.location += currentSpeed * glm::normalize(moveDir) * deltaSeconds;
        }

        // Mouse pan input movement
        const bool wasPanning = m_mousePanEnabled;
        toogleMousePan(context, input.mouseDown(m_keys.mousePan));
        if (wasPanning && m_mousePanEnabled)
        {
            // A displacement like the mouse look: independent of the frame time
            const auto delta = cursorDelta(input);
            glm::vec3 panDir{ 0.0f };
            panDir -= rightDir * delta.x;
            panDir += upDir * delta.y;

            transform.location += panDir * mousePanSpeed;
        }
    }

    void toggleMouseRotate(ScriptContext& context, bool enabled)
    {
        if (m_mousePanEnabled)
            return;

        if (!m_mouseRotateEnabled && enabled)
        {
            context.setCursorMode(aegis::platform::CursorMode::Disabled);
            m_mouseRotateEnabled = true;
        }
        else if (m_mouseRotateEnabled && !enabled)
        {
            context.setCursorMode(aegis::platform::CursorMode::Normal);
            m_mouseRotateEnabled = false;
        }
    }

    void toogleMousePan(ScriptContext& context, bool enabled)
    {
        if (m_mouseRotateEnabled)
            return;

        if (!m_mousePanEnabled && enabled)
        {
            context.setCursorMode(aegis::platform::CursorMode::Disabled);
            m_mousePanEnabled = true;
        }
        else if (m_mousePanEnabled && !enabled)
        {
            context.setCursorMode(aegis::platform::CursorMode::Normal);
            m_mousePanEnabled = false;
        }
    }

    KeyMappings m_keys{};
    bool m_mouseRotateEnabled{ false };
    bool m_mousePanEnabled{ false };
};
}

export namespace aegis::scripting
{
/// @brief Registers KinematicMovementController and its speeds with the type registry
inline auto registerKinematicMovementController(reflect::TypeRegistry& types) -> std::expected<void, reflect::Error>
{
    auto builder = registerScript<KinematicMovementController>(types, "KinematicMovementController");
    if (!builder)
        return std::unexpected{ builder.error() };
    builder->field("moveSpeed", &KinematicMovementController::moveSpeed)
        .field("lookSpeed", &KinematicMovementController::lookSpeed)
        .field("mouseSensitivity", &KinematicMovementController::mouseSensitivity)
        .field("mousePanSpeed", &KinematicMovementController::mousePanSpeed);
    return builder->status();
}
}
