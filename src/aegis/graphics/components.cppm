module;

#include <memory>

export module Aegis.Graphics.Components;

import Aegis.Graphics.MaterialInstance;
import Aegis.Graphics.Mesh;
import Aegis.Graphics.Texture;

export namespace Aegis.Graphics
{
	struct Mesh
	{
		std::shared_ptr<Graphics::StaticMesh> staticMesh;
	};

	struct Material
	{
		std::shared_ptr<Graphics::MaterialInstance> instance;
	};

	struct Environment
	{
		std::shared_ptr<Graphics::Texture> skybox;
		std::shared_ptr<Graphics::Texture> irradiance;
		std::shared_ptr<Graphics::Texture> prefiltered;
		std::shared_ptr<Graphics::Texture> brdfLUT;
	};


}