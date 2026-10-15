export module aegis.scripting.movement.dynamic_movement_controller;

import aegis.scripting.script_base;
import aegis.physics.motion_dynamics;
import aegis.platform;
import aegis.scene.components;

namespace aegis::scripting
{
class DynamicMovementController : public ScriptBase
{
public:
    struct KeyMappings
    {
        aegis::platform::Key moveForward = aegis::platform::Key::Up;
        aegis::platform::Key moveBackward = aegis::platform::Key::Down;
        aegis::platform::Key rotateLeft = aegis::platform::Key::Left;
        aegis::platform::Key rotateRight = aegis::platform::Key::Right;
    };

    virtual void update(float deltaSeconds) override
    {
        auto& transform = get<Transform>();
        auto& dynamics = get<physics::MotionDynamics>();

        bool forwardPressed = input().keyPressed(m_keys.moveForward);
        bool backwardPressed = input().keyPressed(m_keys.moveBackward);
        bool leftPressed = input().keyPressed(m_keys.rotateLeft);
        bool rightPressed = input().keyPressed(m_keys.rotateRight);

        auto speed = dynamics.linearSpeed();
        auto angularSpeed = dynamics.angularSpeed();

        // Directional forces
        glm::vec3 move{ 0.0f };
        if (forwardPressed)
            move += transform.forward() * m_linearforce;
        if (backwardPressed)
            move -= transform.forward() * m_linearforce;
        if (!forwardPressed and !backwardPressed and speed > 0.0f)
            move = -dynamics.moveDirection() * m_linearforce * speed;

        // Angular forces
        glm::vec3 rotation{ 0.0f };
        if (leftPressed)
            rotation += transform.up() * m_angularforce;
        if (rightPressed)
            rotation -= transform.up() * m_angularforce;
        if (!leftPressed and !rightPressed and angularSpeed > 0.0f)
            rotation = -dynamics.angularDirection() * m_angularforce * angularSpeed;

        // Add forces
        dynamics.addLinearForce(move);
        dynamics.addAngularForce(rotation);
    }

private:
    KeyMappings m_keys{};

    float m_linearforce = 20.0f;
    float m_angularforce = 20.0f;
};
}
