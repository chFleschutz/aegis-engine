module;

#include <string>

export module Aegis.Scene.Components;

export import Aegis.Math;
export import Aegis.Scene.Entity;
export import Aegis.Scene.ComponentTraits;

export namespace Aegis
{
	/// @brief Gives a name to the entity
	struct Name
	{
		std::string name = "Entity";
	};

	/// @brief Stores the local transformation of the entity
	struct Transform
	{
		glm::vec3 location{ 0.0f, 0.0f, 0.0f };
		glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 scale{ 1.0f, 1.0f, 1.0f };

		auto forward() const -> glm::vec3 { return Math::forward(rotation); }
		auto right() const -> glm::vec3 { return Math::right(rotation); }
		auto up() const -> glm::vec3 { return Math::up(rotation); }
		auto matrix() const -> glm::mat4 { return Math::tranformationMatrix(location, rotation, scale); }
	};

	/// @brief Transformation of the entity in world space (including parent transforms)
	struct GlobalTransform
	{
		glm::vec3 location{ 0.0f, 0.0f, 0.0f };
		glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 scale{ 1.0f, 1.0f, 1.0f };

		auto forward() const -> glm::vec3 { return Math::forward(rotation); }
		auto right() const -> glm::vec3 { return Math::right(rotation); }
		auto up() const -> glm::vec3 { return Math::up(rotation); }
		auto matrix() const -> glm::mat4 { return Math::tranformationMatrix(location, rotation, scale); }
	};

	/// @brief Stores the parent entity
	/// @note Use Entity::setParent to set the parent of an entity 
	struct Parent
	{
		Scene::Entity entity{};
	};

	/// @brief Stores the next and previous sibling of the entity (for linked list)
	/// @note Use Entity::addChild to add a child to an entity (siblings are automatically updated)
	struct Siblings
	{
		Scene::Entity next{};
		Scene::Entity prev{};
	};

	/// @brief Stores the children entities as the first and last child of a linked list
	/// @note Use Entity::addChild to add a child to an entity
	struct Children
	{
		// TODO: This is currentl broken because the entity does not have a pointer to the scene anymore
		//struct Iterator
		//{
		//	Scene::Entity current{};

		//	auto operator*() const -> Scene::Entity { return current; }
		//	auto operator++() -> Iterator& { current = current.get<Siblings>().next; return *this; }
		//	auto operator==(const Iterator& other) const -> bool { return current == other.current; }
		//};

		//struct ReverseIterator
		//{
		//	Scene::Entity current{};

		//	auto operator*() const -> Scene::Entity { return current; }
		//	auto operator++() -> ReverseIterator& { current = current.get<Siblings>().prev; return *this; }
		//	auto operator==(const ReverseIterator& other) const -> bool { return current == other.current; };
		//};

		std::size_t count = 0;
		Scene::Entity first{};
		Scene::Entity last{};

		//auto begin() const -> Children::Iterator { return { first }; }
		//auto end() const -> Children::Iterator { return { Scene::Entity{} }; }
		//auto rbegin() const -> Children::ReverseIterator { return { last }; }
		//auto rend() const -> Children::ReverseIterator { return { Scene::Entity{} }; }
	};

	struct DynamicTag
	{
		// Used to tag an entity as dynamic (updated every frame)
		// IMPORTANT: This component can ONLY be added/removed during scene initialization
		// 
		// TODO: Add runtime moving of objects between static and dynamic instance buffers 
		// TODO: Handle fragmentation of instance buffers when moving objects
	};

	struct AmbientLight
	{
		glm::vec3 color = { 1.0f, 1.0f, 1.0f };
		float intensity = 0.1f;
	};

	struct DirectionalLight
	{
		glm::vec3 color = { 1.0f, 1.0f, 1.0f };
		float intensity = 1.0f;
	};

	/// @brief Creates a light 
	struct PointLight
	{
		glm::vec3 color = { 1.0f, 1.0f, 1.0f };
		float intensity = 100.0f;
	};

	/// @brief Holds a camera to view the scene
	struct Camera
	{
		float fov = glm::radians(60.0f);
		float aspect = 1.0f;
		float near = 0.1f;
		float far = 1000.0f;

		glm::mat4 viewMatrix = glm::mat4{ 1.0f };
		glm::mat4 inverseViewMatrix = glm::mat4{ 1.0f };
		glm::mat4 projectionMatrix = glm::mat4{ 1.0f };
	};


	// Specialize component traits
	template<>
	struct ComponentTraits<Name>
	{
		static constexpr bool is_required = true;
	};

	template<>
	struct ComponentTraits<Transform>
	{
		static constexpr bool is_required = true;
	};

	template<>
	struct ComponentTraits<GlobalTransform>
	{
		static constexpr bool is_required = true;
	};

	template<>
	struct ComponentTraits<Parent>
	{
		static constexpr bool is_required = true;
	};

	template<>
	struct ComponentTraits<Siblings>
	{
		static constexpr bool is_required = true;
	};

	template<>
	struct ComponentTraits<Children>
	{
		static constexpr bool is_required = true;
	};
}
