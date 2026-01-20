#pragma once

#include <chrono>

#define ENGINE_DIR PROJECT_DIR "/"
#define SHADER_DIR BUILD_DIR "/shaders/"
#define ASSETS_DIR ENGINE_DIR "modules/aegis-assets/"

namespace Aegis::Core
{
	constexpr uint32_t DEFAULT_WIDTH{ 1920 };
	constexpr uint32_t DEFAULT_HEIGHT{ 1080 };

	constexpr bool ENABLE_FPS_LIMIT{ false };
	constexpr uint32_t TARGET_FPS{ 144 };
	constexpr double TARGET_FRAME_TIME{ 1000.0 / static_cast<double>(TARGET_FPS) };

	constexpr uint32_t INVALID_HANDLE{ std::numeric_limits<uint32_t>::max() };
}
