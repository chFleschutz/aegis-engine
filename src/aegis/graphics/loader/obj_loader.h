#pragma once

#include "scene/scene.h"

namespace Aegis::Scene
{
	class OBJLoader
	{
	public:
		OBJLoader(Scene& scene, const std::filesystem::path& path);

		[[nodiscard]] auto rootEntity() const -> Entity { return m_rootEntity; }

	private:
		Entity m_rootEntity;
	};
}