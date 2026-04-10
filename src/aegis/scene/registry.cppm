module;

#include "core/assert.h"

#include <entt/entt.hpp>

export module Aegis.Scene.Registry;

import Aegis.Math;
import Aegis.Scene.Entity;
import Aegis.Scene.Components;
import Aegis.Scene.ComponentTraits;

export namespace Aegis::Scene
{
	class Registry
	{
	public:
		Registry() = default;
		Registry(const Registry&) = delete;
		Registry(Registry&&) = delete;

		auto operator=(const Registry&) -> Registry & = delete;
		auto operator=(Registry&&) -> Registry & = delete;

		[[nodiscard]] auto enttRegistry() -> entt::registry& { return m_registry; }

		/// @brief Creates an entity with a NameComponent and TransformComponent
		/// @note Scene::Entity can be passed by value
		auto create(const std::string& name = std::string(), const glm::vec3& location = glm::vec3{ 0.0f },
			const glm::quat& rotation = glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f }, const glm::vec3& scale = glm::vec3{ 1.0f }) -> Entity
		{
			Entity entity{ m_registry.create() };
			add<Name>(entity, name.empty() ? "Entity" : name);
			add<Transform>(entity, location, rotation, scale);
			add<GlobalTransform>(entity);
			add<Parent>(entity);
			add<Siblings>(entity);
			add<Children>(entity);
			return entity;
		}

		void destroy(Entity entity)
		{
			// TODO: Destroy all scripts attached to the entity

			removeParent(entity);
			removeChildren(entity);

			m_registry.destroy(entity);
		}

		/// @brief Checks if the entity has all components of type T...
		template<typename... T>
		auto has(Entity entity) const -> bool
		{
			return m_registry.all_of<T...>(entity);
		}

		/// @brief Acces to the component of type T
		template<typename T>
		auto get(Entity entity) -> T&
		{
			AGX_ASSERT_X(has<T>(entity), "Cannot get Component: Entity does not have the component");
			return m_registry.get<T>(entity);
		}

		/// @brief Adds a component of type T to the entity
		/// @return A refrence to the new component
		template<typename T, typename... Args>
		auto add(Entity entity, Args&&... args) -> T&
		{
			AGX_ASSERT_X(!has<T>(entity), "Cannot add Component: Entity already has the component");
			return m_registry.emplace<T>(entity, std::forward<Args>(args)...);
		}

		/// @brief Overload to add tag components (empty structs) to the entity
		template<TagComponent T>
		auto add(Entity entity)
		{
			AGX_ASSERT_X(!has<T>(entity), "Cannot add Component: Entity already has the component");
			m_registry.emplace<T>(entity);
		}

		template<typename T>
		auto getOrAdd(Entity entity) -> T&
		{
			return m_registry.get_or_emplace<T>(entity);
		}

		/// @brief Removes a component of type T from the entity
		/// @note Entity MUST have the component and it MUST be an optional component
		template<typename T>
			requires IsOptionalComponent<T>
		void remove(Entity entity)
		{
			AGX_ASSERT_X(has<T>(entity), "Cannot remove Component: Entity does not have the component");
			m_registry.remove<T>(entity);
		}

		template<typename... Components, typename... Exclude>
		auto view(entt::exclude_t<Exclude...> exclusion = entt::exclude_t{})
		{
			return m_registry.view<Components...>(exclusion);
		}


		void setParent(Entity entity, Entity parent)
		{
			AGX_ASSERT_X(entity, "Cannot set parent: Entity is null");
			AGX_ASSERT_X(parent, "Cannot set parent: Parent entity is null");
			AGX_ASSERT_X(parent != entity, "Cannot set parent: Entity cannot be its own parent");

			addChild(parent, entity);
		}

		void removeParent(Entity entity)
		{
			AGX_ASSERT_X(entity, "Cannot remove parent: Entity is null");
			AGX_ASSERT_X(has<Parent>(entity), "Cannot remove parent: Entity does not have a parent");

			auto& parent = get<Parent>(entity);
			if (!parent.entity)
				return;

			removeChild(parent.entity, entity);
			parent.entity = Entity{};
		}

		void addChild(Entity entity, Entity child)
		{
			AGX_ASSERT_X(entity, "Cannot add child: Entity is null");
			AGX_ASSERT_X(child, "Cannot add child: Child entity is null");

			auto& parent = get<Parent>(child);
			if (parent.entity == entity) // Already added as a child
				return;

			parent.entity = entity;

			auto& children = get<Children>(entity);
			if (children.first)
			{
				get<Siblings>(children.last).next = child;
				get<Siblings>(child).prev = children.last;
			}
			else // No children yet
			{
				children.first = child;
			}
			children.last = child;
			children.count++;
		}

		void removeChild(Entity entity, Entity child)
		{
			AGX_ASSERT_X(entity, "Cannot remove child: Entity is null");
			AGX_ASSERT_X(has<Children>(entity), "Cannot remove child: Entity does not have children");
			AGX_ASSERT_X(child, "Cannot remove child: Child entity is null");
			AGX_ASSERT_X(has<Parent>(child), "Cannot remove child: Entity does not have a parent");
			AGX_ASSERT_X(get<Parent>(child).entity == entity, "Cannot remove child: Entity is not a child of this entity");

			auto& children = get<Children>(entity);
			children.count--;

			auto& siblings = get<Siblings>(child);
			Entity prevSibling = siblings.prev;
			Entity nextSibling = siblings.next;

			// Update siblings and first/last child
			if (prevSibling)
			{
				get<Siblings>(prevSibling).next = nextSibling;
			}
			else // First Child
			{
				children.first = siblings.next;
			}

			if (nextSibling)
			{
				get<Siblings>(nextSibling).prev = prevSibling;
			}
			else // Last Child
			{
				children.last = siblings.prev;
			}

			// Remove parent at the end
			get<Parent>(child) = Parent{};
		}

		void removeChildren(Entity entity)
		{
			AGX_ASSERT_X(entity, "Cannot remove children: Entity is null");
			AGX_ASSERT_X(has<Children>(entity), "Cannot remove children: Entity does not have children");

			// This needs to be done in two steps to avoid invalidating the iterator
			auto& children = get<Children>(entity);
			std::vector<Entity> childrenToRemove;
			childrenToRemove.reserve(children.count);

			// TODO: This can be simplified by repairing the iterators for the children
			Entity current = children.first;
			while (current)
			{
				childrenToRemove.emplace_back(current);
				current = get<Siblings>(current).next;
			}

			children = Children{};
			for (auto& child : childrenToRemove)
			{
				get<Parent>(child) = Parent{};
				get<Siblings>(child) = Siblings{};
			}
		}

	private:
		entt::registry m_registry;
	};
}