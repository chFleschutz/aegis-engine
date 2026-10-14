module;

#include <algorithm>
#include <limits>

export module Aegis.Scripting.Movement.KinematicMovementController;

import Aegis.Scripting.ScriptBase;
import Aegis.Math;
import aegis.platform;
import Aegis.Scene.Components;

export namespace Aegis::Scripting
{
	class KinematicMovementController : public ScriptBase
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

		void begin() override
		{
            m_previousCursorPos = cursorPosition();
        }

		void update(float deltaSeconds) override
		{
			applyRotation(deltaSeconds);
			applyMovement(deltaSeconds);
		}

		/// @brief Sets new overall move speed
		void setMoveSpeed(float speed) { m_moveSpeed = speed; }
		/// @brief Sets new overall look speed
		void setLookSpeed(float speed) { m_lookSpeed = speed; }
		/// @brief Sets speed for looking with the mouse
		void setMouseSensitivity(float sensitivity) { m_mouseSensitivity = sensitivity; }

	private:
        [[nodiscard]] auto cursorPosition() const -> glm::vec2
        {
            const auto [x, y] = input().cursorPosition();
            return { static_cast<float>(x), static_cast<float>(y) };
        }

        void applyRotation(float deltaSeconds)
		{
			auto& transform = get<Transform>();

			// Key input rotation
			glm::vec3 rotate{ 0.0f };
            if (input().keyPressed(m_keys.lookRight))
                rotate.z -= m_lookSpeed;
            if (input().keyPressed(m_keys.lookLeft))
                rotate.z += m_lookSpeed;
            if (input().keyPressed(m_keys.lookUp))
                rotate.x += m_lookSpeed;
            if (input().keyPressed(m_keys.lookDown))
                rotate.x -= m_lookSpeed;

            // Mouse input rotation
            toggleMouseRotate(input().mouseButtonPressed(m_keys.mouseRotate));
            if (m_mouseRotateEnabled)
			{
                auto cursorPos = cursorPosition();
                rotate.x -= (cursorPos.y - m_previousCursorPos.y) * m_mouseSensitivity;
				rotate.z -= (cursorPos.x - m_previousCursorPos.x) * m_mouseSensitivity;
				m_previousCursorPos = cursorPos;
			}

			if (glm::length(rotate) > std::numeric_limits<float>::epsilon())
			{
				rotate *= deltaSeconds;

				glm::vec3 rotation = glm::eulerAngles(transform.rotation);
				rotation.x = glm::clamp(rotation.x + rotate.x, -1.5f, 1.5f);
				rotation.z = glm::mod(rotation.z + rotate.z, glm::two_pi<float>());
				rotation.y = 0.0f; // Lock rotation around y-axis
				transform.rotation = glm::quat(rotation);
			}
		}

		void applyMovement(float deltaSeconds)
		{
			auto& transform = get<Transform>();
			auto forwardDir = transform.forward();
			auto rightDir = transform.right();
			auto upDir = Math::World::UP;

			// Speed modifiers
            if (input().keyPressed(m_keys.increaseSpeed))
                m_moveSpeed += 100.0f * deltaSeconds;
            if (input().keyPressed(m_keys.decreaseSpeed))
                m_moveSpeed -= 100.0f * deltaSeconds;
            if (input().keyPressed(m_keys.resetSpeed))
                m_moveSpeed = DEFAULT_MOVE_SPEED;
            m_moveSpeed = std::max(m_moveSpeed, 0.1f);

			auto currentSpeed = m_moveSpeed;
            if (input().keyPressed(m_keys.speedupModifier))
                currentSpeed *= 2.0f;
            if (input().keyPressed(m_keys.slowdownModifier))
                currentSpeed *= 0.5f;

            // Key input movement
			glm::vec3 moveDir{ 0.0f };
            if (input().keyPressed(m_keys.moveForward))
                moveDir += forwardDir;
            if (input().keyPressed(m_keys.moveBackward))
                moveDir -= forwardDir;
            if (input().keyPressed(m_keys.moveRight))
                moveDir += rightDir;
            if (input().keyPressed(m_keys.moveLeft))
                moveDir -= rightDir;
            if (input().keyPressed(m_keys.moveUp))
                moveDir += upDir;
            if (input().keyPressed(m_keys.moveDown))
                moveDir -= upDir;

            if (glm::length(moveDir) > std::numeric_limits<float>::epsilon())
			{
				transform.location += currentSpeed * glm::normalize(moveDir) * deltaSeconds;
			}

			// Mouse pan input movement
            toogleMousePan(input().mouseButtonPressed(m_keys.mousePan));
            if (m_mousePanEnabled)
			{
                auto cursorPos = cursorPosition();
                moveDir -= rightDir * (cursorPos.x - m_previousCursorPos.x);
				moveDir += upDir * (cursorPos.y - m_previousCursorPos.y);
				m_previousCursorPos = cursorPos;

				if (glm::length(moveDir) > std::numeric_limits<float>::epsilon())
				{
					transform.location += moveDir * deltaSeconds;
				}
			}
		}

		void toggleMouseRotate(bool enabled)
		{
			if (m_mousePanEnabled)
				return;

			if (!m_mouseRotateEnabled && enabled)
			{
                input().setCursorMode(aegis::platform::CursorMode::Disabled);
                m_mouseRotateEnabled = true;
                m_previousCursorPos = cursorPosition();
            }
			else if (m_mouseRotateEnabled && !enabled)
			{
                input().setCursorMode(aegis::platform::CursorMode::Normal);
                m_mouseRotateEnabled = false;
			}
		}

		void toogleMousePan(bool enabled)
		{
			if (m_mouseRotateEnabled)
				return;

			if (!m_mousePanEnabled && enabled)
			{
                input().setCursorMode(aegis::platform::CursorMode::Disabled);
                m_mousePanEnabled = true;
                m_previousCursorPos = cursorPosition();
            }
			else if (m_mousePanEnabled && !enabled)
			{
                input().setCursorMode(aegis::platform::CursorMode::Normal);
                m_mousePanEnabled = false;
			}
		}

		KeyMappings m_keys{};
		float m_moveSpeed{ DEFAULT_MOVE_SPEED };
		float m_lookSpeed{ 1.5f };
		float m_mouseSensitivity{ 0.25f };
		bool m_mouseRotateEnabled{ false };
		bool m_mousePanEnabled{ false };

		glm::vec2 m_previousCursorPos{ 0.0f };
	};
}
