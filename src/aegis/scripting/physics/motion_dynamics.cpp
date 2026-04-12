#include "pch.h"
#include "motion_dynamics.h"

#include "scene/components.h"

namespace Aegis::Physics
{
	Force& Force::operator+=(const Force& other)
	{
		linear += other.linear;
		angular += other.angular;
		return *this;
	}

	Force& Force::operator-=(const Force& other)
	{
		linear -= other.linear;
		angular -= other.angular;
		return *this;
	}

	Force& Force::operator*=(const Force& other)
	{
		linear *= other.linear;
		angular *= other.angular;
		return *this;
	}

	Force& Force::operator/=(const Force& other)
	{
		linear /= other.linear;
		angular /= other.angular;
		return *this;
	}

	Force& Force::operator*=(float scalar)
	{
		linear *= scalar;
		angular *= scalar;
		return *this;
	}

	Force& Force::operator/=(float scalar)
	{
		linear /= scalar;
		angular /= scalar;
		return *this;
	}


	void MotionDynamics::addForce(const Force& force)
	{
		// TODO: use force instead of linear and angular
		addLinearForce(force.linear);
		addAngularForce(force.angular);
	}

	void MotionDynamics::addLinearForce(const glm::vec3& force)
	{
		m_accumulatedLinearForce += force;
	}

	void MotionDynamics::addAngularForce(const glm::vec3& angularForce)
	{
		m_accumulatedAngularForce += angularForce;
	}

	void MotionDynamics::haltMotion()
	{
		m_linearVelocity = glm::vec3{ 0.0f };
		m_angularVelocity = glm::vec3{ 0.0f };
	}

	glm::vec3 MotionDynamics::moveDirection() const
	{
		AGX_ASSERT_X(linearSpeed() > 0.0f, "Direction of zero-vector is undefined");
		return glm::normalize(m_linearVelocity);
	}

	glm::vec3 MotionDynamics::angularDirection() const
	{
		AGX_ASSERT_X(angularSpeed() > 0.0f, "Direction of zero-vector is undefined");
		return glm::normalize(m_angularVelocity);
	}

	void MotionDynamics::update(float deltaSeconds)
	{
		addFriction(deltaSeconds);

		applyForces(deltaSeconds);
		// Update transform
		auto& transform = get<Transform>();
		transform.location += m_linearVelocity * deltaSeconds;
		transform.rotation *= glm::quat(m_angularVelocity * deltaSeconds);
	}

	void MotionDynamics::applyForces(float deltaSeconds)
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

	void MotionDynamics::addFriction(float deltaSeconds)
	{
		addLinearForce(-m_linearVelocity * m_properties.linearFriction);
		addAngularForce(-m_angularVelocity * m_properties.angularFriction);
	}
}
