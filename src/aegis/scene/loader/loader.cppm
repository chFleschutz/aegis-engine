module;

#include <filesystem>

export module Aegis.Scene.Loader;

import Aegis.Scene.Entity;
import Aegis.Scene.OBJLoader;
import Aegis.Scene.GLTFLoader;

export namespace Aegis::Scene
{
	class Loader
	{
	public:
		Loader() = delete;

		static auto load(Scene& scene, const std::filesystem::path& path) -> Entity
		{
			if (path.extension() == ".gltf" || path.extension() == ".glb")
			{
				//GLTFLoader loader{ *this, path };
				FastGLTFLoader loader{ *this, path };
				return loader.rootEntity();
			}
			else if (path.extension() == ".obj")
			{
				OBJLoader loader{ *this, path };
				return loader.rootEntity();
			}
			else
			{
				AGX_ASSERT_X(false, "Unsupported file format");
			}

			return Entity{};
		}
	};
}

