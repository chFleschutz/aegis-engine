module;

#include <chrono>
#include <filesystem>

export module Aegis.Core.Globals;

export namespace Aegis::Core
{
	constexpr uint32_t DEFAULT_WIDTH{ 1920 };
	constexpr uint32_t DEFAULT_HEIGHT{ 1080 };

	constexpr bool ENABLE_FPS_LIMIT{ false };
	constexpr uint32_t TARGET_FPS{ 144 };
	constexpr double TARGET_FRAME_TIME{ 1000.0 / static_cast<double>(TARGET_FPS) };

	constexpr uint32_t INVALID_HANDLE{ std::numeric_limits<uint32_t>::max() };

	export const std::filesystem::path ENGINE_DIR{ PROJECT_DIR "/"};
	export const std::filesystem::path SHADER_DIR{ BUILD_DIR "/shaders/" };
	export const std::filesystem::path ASSETS_DIR{ PROJECT_DIR "/modules/aegis-assets/" };
}
