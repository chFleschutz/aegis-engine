module;

#include <cstdint>

export module Aegis.Graphics.Globals;

export namespace Aegis::Graphics
{
#ifdef NDEBUG
	constexpr bool ENABLE_VALIDATION = false;
#else
	constexpr bool ENABLE_VALIDATION = true;
#endif

	constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
	constexpr uint32_t MAX_POINT_LIGHTS = 128;
}