module;

#include "core/assert.h"

#include <cstdint>
#include <filesystem>
#include <unordered_map>

export module Aegis.Core.AssetManager;

import Aegis.Core.Asset;

export namespace Aegis::Core
{
	using AssetID = uint64_t;

	class AssetManager
	{
	public:
		AssetManager() = default;
		~AssetManager() = default;

		template<IsAsset T>
		[[nodiscard]] auto get(const std::filesystem::path& path) -> std::shared_ptr<T>
		{
			AssetID id = std::hash<std::filesystem::path>{}(path);
			auto it = m_assets.find(id);
			if (it != m_assets.end())
			{
				auto asset = std::dynamic_pointer_cast<T>(it->second);
				AGX_ASSERT_X(asset, "Asset found but type mismatch");
				return asset;
			}

			// TODO: Implement asset loading from file
			AGX_ASSERT_X(false, "Asset loading not implemented yet");
			return nullptr;
		}

		template<IsAsset T>
		void add(const std::filesystem::path& path, const std::shared_ptr<T>& asset)
		{
			AssetID id = std::hash<std::filesystem::path>{}(path);
			asset->m_path = path;
			m_assets[id] = asset;
		}

		void garbageCollect()
		{
			for (auto it = m_assets.begin(); it != m_assets.end(); )
			{
				if (it->second.use_count() == 1)
					it = m_assets.erase(it);
				else
					++it;
			}
		}

	private:
		// TODO: Use a weak_ptr for auto release of assets (needs asset file loading first to load on demand)
		std::unordered_map<AssetID, std::shared_ptr<Asset>> m_assets;
	};
}