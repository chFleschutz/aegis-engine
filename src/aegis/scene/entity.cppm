module;

#include <entt/entt.hpp>

export module Aegis.Scene.Entity;

export namespace entt
{
	using entt::entity;
	using entt::registry;
	using entt::view;
	using entt::basic_view;
	using entt::exclude_t;
	using entt::exclude;

	using entt::operator==;
	using entt::operator!=;

	namespace internal
	{
		using entt::internal::operator==;
		using entt::internal::operator!=;
	}
}

export namespace Aegis::Scene
{
	/// @brief An entity represents any object in a scene
	/// @note This class is ment to be passed by value since its just an id
	class Entity
	{
	public:
		Entity() = default;
		explicit Entity(entt::entity entityHandle) : m_id{ entityHandle } {}

		auto operator==(const Entity&) const -> bool = default;
		auto operator!=(const Entity&) const -> bool = default;

		operator bool() const { return m_id != entt::null; }

		[[nodiscard]] auto id() const -> entt::entity { return m_id; }

	private:
		entt::entity m_id{ entt::null };
	};
}
