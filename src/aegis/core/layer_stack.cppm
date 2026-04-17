module;

#include <vector>
#include <memory>

export module Aegis.Core.LayerStack;

import Aegis.Core.Layer;

export namespace Aegis::Core
{
	class LayerStack
	{
	public:
		LayerStack() = default;
		~LayerStack()
		{
			for (auto& layer : m_Layers)
			{
				layer->onDetach();
			}
		}

		template<typename T, typename... Args>
		T& push(Args&&... args)
		{
			auto layer = std::make_unique<T>(std::forward<Args>(args)...);
			auto& result = *layer;
			m_Layers.emplace_back(std::move(layer));
			result.onAttach();
			return result;
		}

		void update(float deltaSeconds)
		{
			for (auto& layer : m_Layers)
			{
				layer->onUpdate(deltaSeconds);
			}
		}

		auto begin() { return m_Layers.begin(); }
		auto end() { return m_Layers.end(); }

	private:
		std::vector<std::unique_ptr<Layer>> m_Layers;
	};
}