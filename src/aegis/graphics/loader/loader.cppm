module;

#include "core/assert.h"

#include <filesystem>

export module Aegis.Graphics.Loader;

import :OBJLoader;
import :GLTFLoader;
import :FastGLTFLoader;
import Aegis.Scene.Registry;

export namespace Aegis::Graphics
{
	class Loader
	{
	public:
		Loader() = delete;

		static auto load(Scene::Registry& scene, const std::filesystem::path& path) -> Scene::Entity
		{
			if (path.extension() == ".gltf" || path.extension() == ".glb")
			{
				//GLTFLoader loader{ *this, path };
				FastGLTFLoader loader{ scene, path };
				return loader.rootEntity();
			}
			else if (path.extension() == ".obj")
			{
				OBJLoader loader{ scene, path };
				return loader.rootEntity();
			}
			else
			{
				AGX_ASSERT_X(false, "Unsupported file format");
			}

			return Scene::Entity{};
		}
	};
}

