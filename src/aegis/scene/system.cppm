module;

#include <concepts>

export module Aegis.Scene.System;

export import Aegis.Scene.Registry;

export namespace Aegis::Scene
{
	class System
	{
	public:
		System() = default;
		virtual ~System() = default;

		virtual void onAttach() {}
		virtual void onDetach() {}
		virtual void onBegin(Registry& registry) {}
		virtual void onUpdate(Registry& registry, float deltaSeconds) {}
	};

	template <typename T>
	concept SystemDerived = std::derived_from<T, System>;
}