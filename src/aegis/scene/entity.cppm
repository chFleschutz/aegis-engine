module;

#include "core/assert.h"

#include <entt/entt.hpp>

#include <concepts>

export module Aegis.Scene.Entity;

//import Aegis.Scene.ComponentTraits;

//namespace Aegis::Scripting
//{
//	class ScriptBase;
//
//	template<typename T>
//	concept Script = std::is_base_of_v<ScriptBase, T>;
//}

export namespace Aegis::Scene
{
	//template<typename T>
	//concept TagComponent = std::is_empty_v<T>;

	/// @brief An entity represents any object in a scene
	/// @note This class is ment to be passed by value since its just an id
	class Entity
	{
	public:
		Entity() = default;
		Entity(entt::entity entityHandle/*, Scene* scene*/)
			: m_id(entityHandle) //, m_scene(scene)
		{
			//AGX_ASSERT_X(m_scene, "Entity must have a scene");
		}

		Entity(const Entity&) = default;
		Entity(Entity&&) = default;

		auto operator=(const Entity&) -> Entity & = default;
		auto operator=(Entity&&) -> Entity & = default;

		//bool operator==(const Entity& other) const
		//{
		//	return m_id == other.m_id && m_scene == other.m_scene;
		//}

		//bool operator!=(const Entity& other) const
		//{
		//	return !(*this == other);
		//}

		operator bool() const { return m_id != entt::null; }
		operator entt::entity() const { return m_id; }
		operator uint32_t() const { return static_cast<uint32_t>(m_id); }

		//[[nodiscard]] auto registry() const -> entt::registry&
		//{
		//	return m_scene->m_registry;
		//}

		///// @brief Checks if the entity has all components of type T...
		//template<typename... T>
		//auto has() const -> bool
		//{
		//	return registry().all_of<T...>(m_id);
		//}

		///// @brief Acces to the component of type T
		//template<typename T>
		//auto get() const -> T&
		//{
		//	AGX_ASSERT_X(has<T>(), "Cannot get Component: Entity does not have the component");
		//	return registry().get<T>(m_id);
		//}

		///// @brief Adds a component of type T to the entity
		///// @return A refrence to the new component
		//template<typename T, typename... Args>
		//auto add(Args&&... args) -> T&
		//{
		//	AGX_ASSERT_X(!has<T>(), "Cannot add Component: Entity already has the component");
		//	return registry().emplace<T>(m_id, std::forward<Args>(args)...);
		//}

		///// @brief Overload to add tag components (empty structs) to the entity
		//template<TagComponent T>
		//auto add()
		//{
		//	AGX_ASSERT_X(!has<T>(), "Cannot add Component: Entity already has the component");
		//	registry().emplace<T>(m_id);
		//}

		///// @brief Overload to add a script derived from Aegis::Scripting::ScriptBase to the entity
		///// @return A refrence to the new script
		//template<Scripting::Script T, typename... Args>
		//auto add(Args&&... args) -> T&
		//{
		//	AGX_ASSERT_X(!has<T>(), "Cannot add Component: Entity already has the component");
		//	auto& script = registry().emplace<T>(m_id, std::forward<Args>(args)...);
		//	addScript(&script);
		//	return script;
		//}

		//template<typename T>
		//auto getOrAdd() -> T&
		//{
		//	return registry().get_or_emplace<T>(m_id);
		//}

		///// @brief Removes a component of type T from the entity
		///// @note Entity MUST have the component and it MUST be an optional component
		//template<typename T>
		//	requires IsOptionalComponent<T>
		//void remove()
		//{
		//	AGX_ASSERT_X(has<T>(), "Cannot remove Component: Entity does not have the component");
		//	registry().remove<T>(m_id);
		//}

		//void setParent(Entity parent)
		//{
		//	AGX_ASSERT_X(*this, "Cannot set parent: Entity is null");
		//	AGX_ASSERT_X(parent, "Cannot set parent: Parent entity is null");
		//	AGX_ASSERT_X(parent != *this, "Cannot set parent: Entity cannot be its own parent");

		//	parent.addChild(*this);
		//}

		//void removeParent()
		//{
		//	AGX_ASSERT_X(*this, "Cannot remove parent: Entity is null");
		//	AGX_ASSERT_X(has<Parent>(), "Cannot remove parent: Entity does not have a parent");

		//	auto& parent = get<Parent>();
		//	if (!parent.entity)
		//		return;

		//	parent.entity.removeChild(Entity{ *this });
		//	parent.entity = Entity{};
		//}

		//void addChild(Entity child)
		//{
		//	AGX_ASSERT_X(*this, "Cannot add child: Entity is null");
		//	AGX_ASSERT_X(child, "Cannot add child: Child entity is null");

		//	auto& parent = child.get<Parent>();
		//	if (parent.entity == *this) // Already added as a child
		//		return;

		//	parent.entity = *this;

		//	auto& children = get<Children>();
		//	if (children.first)
		//	{
		//		children.last.get<Siblings>().next = child;
		//		child.get<Siblings>().prev = children.last;
		//	}
		//	else // No children yet
		//	{
		//		children.first = child;
		//	}
		//	children.last = child;
		//	children.count++;
		//}

		//void removeChild(Entity child)
		//{
		//	AGX_ASSERT_X(*this, "Cannot remove child: Entity is null");
		//	AGX_ASSERT_X(has<Children>(), "Cannot remove child: Entity does not have children");
		//	AGX_ASSERT_X(child, "Cannot remove child: Child entity is null");
		//	AGX_ASSERT_X(child.has<Parent>(), "Cannot remove child: Entity does not have a parent");
		//	AGX_ASSERT_X(child.get<Parent>().entity == *this, "Cannot remove child: Entity is not a child of this entity");

		//	auto& children = get<Children>();
		//	children.count--;

		//	auto& siblings = child.get<Siblings>();
		//	Entity prevSibling = siblings.prev;
		//	Entity nextSibling = siblings.next;

		//	// Update siblings and first/last child
		//	if (prevSibling)
		//	{
		//		prevSibling.get<Siblings>().next = nextSibling;
		//	}
		//	else // First Child
		//	{
		//		children.first = siblings.next;
		//	}

		//	if (nextSibling)
		//	{
		//		nextSibling.get<Siblings>().prev = prevSibling;
		//	}
		//	else // Last Child
		//	{
		//		children.last = siblings.prev;
		//	}

		//	// Remove parent at the end
		//	child.get<Parent>() = Parent{};
		//}

		//void removeChildren()
		//{
		//	AGX_ASSERT_X(*this, "Cannot remove children: Entity is null");
		//	AGX_ASSERT_X(has<Children>(), "Cannot remove children: Entity does not have children");

		//	// This needs to be done in two steps to avoid invalidating the iterator
		//	auto& children = get<Children>();
		//	std::vector<Entity> childrenToRemove;
		//	childrenToRemove.reserve(children.count);
		//	for (auto child : children)
		//	{
		//		childrenToRemove.emplace_back(child);
		//	}

		//	children = Children{};
		//	for (auto& child : childrenToRemove)
		//	{
		//		child.get<Parent>() = Parent{};
		//		child.get<Siblings>() = Siblings{};
		//	}
		//}

	private:
		//void addScript(Scripting::ScriptBase* script)
		//{
		//	script->m_entity = *this;
		//	m_scene->addScript(script);
		//}

		entt::entity m_id{ entt::null };
	};
}
