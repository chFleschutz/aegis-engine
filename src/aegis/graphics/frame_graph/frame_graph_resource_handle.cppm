module;

#include <limits>
#include <cstdint>

export module Aegis.Graphics.FrameGraph.ResourceHandle;

export namespace Aegis::Graphics
{
	struct FGHandle
	{
		static constexpr uint32_t INVALID_HANDLE = std::numeric_limits<uint32_t>::max();

		uint32_t handle{ INVALID_HANDLE };

		[[nodiscard]] auto operator==(const FGHandle& other) const -> bool { return handle == other.handle; }
		[[nodiscard]] auto isValid() const -> bool { return handle != INVALID_HANDLE; }
	};

	struct FGNodeHandle : FGHandle {};
	struct FGResourceHandle : FGHandle {};
	struct FGBufferHandle : FGHandle {};
	struct FGTextureHandle : FGHandle {};

}