export module aegis.core.globals;

import std;

export namespace aegis::core
{
constexpr std::uint32_t DEFAULT_WIDTH{ 1920 };
constexpr std::uint32_t DEFAULT_HEIGHT{ 1080 };

constexpr bool ENABLE_FPS_LIMIT{ false };
constexpr std::uint32_t TARGET_FPS{ 144 };
constexpr double TARGET_FRAME_TIME{ 1000.0 / static_cast<double>(TARGET_FPS) };

constexpr std::uint32_t INVALID_HANDLE{ std::numeric_limits<std::uint32_t>::max() };

const std::filesystem::path ENGINE_DIR{ PROJECT_DIR "/" };
const std::filesystem::path SHADER_DIR{ BUILD_DIR "/shaders/" };
const std::filesystem::path ASSETS_DIR{ PROJECT_DIR "/modules/aegis-assets/" };
}
