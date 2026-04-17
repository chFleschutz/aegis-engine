module;

#include <cstdint>

export module Aegis.Graphics.Globals;

namespace Aegis::Graphics
{
#ifdef NDEBUG
	export constexpr bool ENABLE_VALIDATION = false;
#else
	export constexpr bool ENABLE_VALIDATION = true;
#endif

	export constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
	export constexpr uint32_t MAX_POINT_LIGHTS = 128;
}