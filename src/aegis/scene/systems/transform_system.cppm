module;

#include <vector>
#include <ranges>

export module Aegis.Scene.TransformSystem;

import Aegis.Scene.Registry;
import Aegis.Scene.Components;
import Aegis.Scene.Entity;
import Aegis.Scene.System;

export namespace Aegis::Scene
{
	class TransformSystem : public System
	{
	public:
		TransformSystem() = default;
		~TransformSystem() = default;

		void onBegin(Registry& registry) override
		{
			// Calculate initial global transforms for all entities
			std::vector<Entity> hierarchy;
			auto view = registry.view<GlobalTransform>();
			for (auto&& [entity, globalTransform] : view.each())
			{
				// Find hierarchy of all parents
				hierarchy.clear();
				Entity current = Entity{ entity };
				while (current)
				{
					hierarchy.emplace_back(current);
					current = registry.get<Parent>(current).entity;
				}

				globalTransform = GlobalTransform{};
				for (auto e : std::ranges::views::reverse(hierarchy))
				{
					auto& localTransform = registry.get<Transform>(e);
					globalTransform.location = globalTransform.location + localTransform.location;
					globalTransform.rotation = globalTransform.rotation * localTransform.rotation;
					globalTransform.scale = globalTransform.scale * localTransform.scale;
				}
			}
		}

		void onUpdate(Registry& registry, float deltaSeconds) override
		{
			// Note: This lags 1 frame per parent behind the actual local transform (but this is fine :)
			auto view = registry.view<Transform, GlobalTransform, Parent, DynamicTag>();
			for (auto&& [entity, transform, globalTransform, parent] : view.each())
			{
				if (!parent.entity || !registry.has<GlobalTransform>(parent.entity))
				{
					globalTransform.location = transform.location;
					globalTransform.rotation = transform.rotation;
					globalTransform.scale = transform.scale;
					continue;
				}

				auto& parentGlobal = registry.get<GlobalTransform>(parent.entity);
				globalTransform.location = parentGlobal.location + transform.location;
				globalTransform.rotation = parentGlobal.rotation * transform.rotation;
				globalTransform.scale = parentGlobal.scale * transform.scale;
			}
		}
	};
}