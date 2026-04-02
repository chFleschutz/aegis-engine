module;

#include <chrono>
#include <string_view>

export module Aegis.Core.Globals;

export namespace Aegis::Core
{
	constexpr uint32_t DEFAULT_WIDTH{ 1920 };
	constexpr uint32_t DEFAULT_HEIGHT{ 1080 };

	constexpr bool ENABLE_FPS_LIMIT{ false };
	constexpr uint32_t TARGET_FPS{ 144 };
	constexpr double TARGET_FRAME_TIME{ 1000.0 / static_cast<double>(TARGET_FPS) };

	constexpr uint32_t INVALID_HANDLE{ std::numeric_limits<uint32_t>::max() };

	constexpr std::string_view ENGINE_DIR{ PROJECT_DIR "/"};
	constexpr std::string_view SHADER_DIR{ BUILD_DIR "/shaders/" };
	constexpr std::string_view ASSETS_DIR{ PROJECT_DIR "modules/aegis-assets/" };
}
