module;

#include "core/assert.h"

#include <entt/entt.hpp>

#include <concepts>

export module Aegis.Scene.Entity;

export namespace Aegis::Scene
{
	/// @brief An entity represents any object in a scene
	/// @note This class is ment to be passed by value since its just an id
	class Entity
	{
	public:
		Entity() = default;
		explicit Entity(entt::entity entityHandle/*, Scene* scene*/)
			: m_id(entityHandle) //, m_scene(scene)
		{
			//AGX_ASSERT_X(m_scene, "Entity must have a scene");
		}

		Entity(const Entity&) = default;
		Entity(Entity&&) = default;

		auto operator=(const Entity&) -> Entity & = default;
		auto operator=(Entity&&) -> Entity & = default;

		auto operator<=>(const Entity&) const = default;

		operator bool() const { return m_id != entt::null; }
		operator entt::entity() const { return m_id; }
		operator uint32_t() const { return static_cast<uint32_t>(m_id); }

	private:
		entt::entity m_id{ entt::null };
	};
}
