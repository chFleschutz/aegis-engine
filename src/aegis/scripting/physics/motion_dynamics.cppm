module;

#include "core/assert.h"

export module Aegis.Physics.MotionDynamics;

import Aegis.Math;
import Aegis.Scripting.ScriptBase;
import Aegis.Scene.Components;

export namespace Aegis::Physics
{
	/// @brief Represents a directional and angular force.
	struct Force
	{
		glm::vec3 linear = glm::vec3{ 0.0f };
		glm::vec3 angular = glm::vec3{ 0.0f };

		Force operator+(const Force& other) const { return { linear + other.linear, angular + other.angular }; }
		Force operator-(const Force& other) const { return { linear - other.linear, angular - other.angular }; }
		Force operator*(const Force& other) const { return { linear * other.linear, angular * other.angular }; }
		Force operator/(const Force& other) const { return { linear / other.linear, angular / other.angular }; }
		Force operator*(float scalar) const { return { linear * scalar, angular * scalar }; }
		Force operator/(float scalar) const { return { linear / scalar, angular / scalar }; }

		Force& operator+=(const Force& other)
		{
			linear += other.linear;
			angular += other.angular;
			return *this;
		}

		Force& operator-=(const Force& other)
		{
			linear -= other.linear;
			angular -= other.angular;
			return *this;
		}

		Force& operator*=(const Force& other)
		{
			linear *= other.linear;
			angular *= other.angular;
			return *this;
		}

		Force& operator/=(const Force& other)
		{
			linear /= other.linear;
			angular /= other.angular;
			return *this;
		}

		Force& operator*=(float scalar)
		{
			linear *= scalar;
			angular *= scalar;
			return *this;
		}

		Force& operator/=(float scalar)
		{
			linear /= scalar;
			angular /= scalar;
			return *this;
		}
	};


	/// @brief Adds motion dynamics to an object to update its position and rotation each frame.
	class MotionDynamics : public Scripting::ScriptBase
	{
	public:
		struct Properties
		{
			float mass = 1.0f;

			float linearFriction = 1.0f;
			float angularFriction = 1.0f;

			float maxLinearSpeed = 5.0f;
			float maxAngularSpeed = 5.0f;
		};

		/// @brief Adds a force to the object.
		/// @param force Directional and angular force to add.
		void addForce(const Force& force)
		{
			// TODO: use force instead of linear and angular
			addLinearForce(force.linear);
			addAngularForce(force.angular);
		}

		/// @brief Adds a directional force to the object.
		/// @param force Directional force to add.
		void addLinearForce(const glm::vec3& force)
		{
			m_accumulatedLinearForce += force;
		}

		/// @brief Adds an angular force to the object.
		/// @param angularForce Angular force to add.
		void addAngularForce(const glm::vec3& angularForce)
		{
			m_accumulatedAngularForce += angularForce;
		}

		/// @brief Halts all motion (sets velocities to zero).
		void haltMotion()
		{
			m_linearVelocity = glm::vec3{ 0.0f };
			m_angularVelocity = glm::vec3{ 0.0f };
		}

		/// @brief Returns the current directional velocity.
		glm::vec3 linearVelocity() const { return m_linearVelocity; }

		/// @brief Returns the current angular velocity.
		glm::vec3 angularVelocity() const { return m_angularVelocity; }

		/// @brief Returns the current directional speed.
		float linearSpeed() const { return glm::length(m_linearVelocity); }

		/// @brief Returns the current angular speed.
		float angularSpeed() const { return glm::length(m_angularVelocity); }

		/// @brief Returns a normalized vector in the current movement direction.
		glm::vec3 moveDirection() const
		{
			AGX_ASSERT_X(linearSpeed() > 0.0f, "Direction of zero-vector is undefined");
			return glm::normalize(m_linearVelocity);
		}

		/// @brief Returns a normalized vector in the current angular direction.
		glm::vec3 angularDirection() const
		{
			AGX_ASSERT_X(angularSpeed() > 0.0f, "Direction of zero-vector is undefined");
			return glm::normalize(m_angularVelocity);
		}

		Properties& properties() { return m_properties; }

		/// @brief Updates the position and rotation of the object based on the current velocity.
		/// @note This function is called automatically each frame.
		void update(float deltaSeconds) override
		{
			addFriction(deltaSeconds);

			applyForces(deltaSeconds);
			// Update transform
			auto& transform = get<Transform>();
			transform.location += m_linearVelocity * deltaSeconds;
			transform.rotation *= glm::quat(m_angularVelocity * deltaSeconds);
		}

	private:
		/// @brief Computes the velocities from the accelerations.
		/// @note Resets the accelerations to zero for the next frame.
		void applyForces(float deltaSeconds)
		{
			// Calculate new velocities
			m_linearVelocity += m_accumulatedLinearForce / m_properties.mass * deltaSeconds;
			m_angularVelocity += m_accumulatedAngularForce / m_properties.mass * deltaSeconds;

			// Limit velocities
			m_linearVelocity *= std::min(1.0f, m_properties.maxLinearSpeed / linearSpeed());
			m_angularVelocity *= std::min(1.0f, m_properties.maxAngularSpeed / angularSpeed());

			// Reset accumulated forces
			m_accumulatedLinearForce = glm::vec3{ 0.0f };
			m_accumulatedAngularForce = glm::vec3{ 0.0f };
		}

		void addFriction(float deltaSeconds)
		{
			addLinearForce(-m_linearVelocity * m_properties.linearFriction);
			addAngularForce(-m_angularVelocity * m_properties.angularFriction);
		}

		Properties m_properties;

		glm::vec3 m_linearVelocity{ 0.0f };
		glm::vec3 m_angularVelocity{ 0.0f };

		glm::vec3 m_accumulatedLinearForce{ 0.0f };
		glm::vec3 m_accumulatedAngularForce{ 0.0f };
	};
}
