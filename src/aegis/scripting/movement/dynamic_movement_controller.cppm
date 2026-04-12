export module Aegis.Scripting.Movement.DynamicMovementController;

import Aegis.Scripting.ScriptBase;
import Aegis.Physics.MotionDynamics;
import Aegis.Core.Input;

namespace Aegis::Scripting
{
	class DynamicMovementController : public ScriptBase
	{
	public:
		struct KeyMappings
		{
			Input::Key moveForward = Input::Up;
			Input::Key moveBackward = Input::Down;
			Input::Key rotateLeft = Input::Left;
			Input::Key rotateRight = Input::Right;
		};

		virtual void update(float deltaSeconds) override
		{
			auto& transform = get<Transform>();
			auto& dynamics = get<Physics::MotionDynamics>();

			bool forwardPressed = Input::instance().keyPressed(m_keys.moveForward);
			bool backwardPressed = Input::instance().keyPressed(m_keys.moveBackward);
			bool leftPressed = Input::instance().keyPressed(m_keys.rotateLeft);
			bool rightPressed = Input::instance().keyPressed(m_keys.rotateRight);

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
