module;

#include "core/assert.h"
#include "graphics/vulkan/vulkan_include.h"

#include <vk_mem_alloc.h>

#include <variant>

export module Aegis.Graphics.FrameGraph.Resource;

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

	// TODO: Simplify by seperating shader stage and usage
	enum class FGResourceUsage
	{
		None,
		ColorAttachment,
		DepthStencilAttachment,
		FragmentReadSampled,
		ComputeReadStorage,
		ComputeWriteStorage,
		ComputeReadSampled,
		TransferSrc,
		TransferDst,
		Present,
	};

	enum class FGResizeMode
	{
		Fixed,
		SwapChainRelative,
	};

	struct FGBufferInfo
	{
		VkDeviceSize size;
		uint32_t instanceCount{ 1 };
		VkBufferUsageFlags usage{ 0 };
		VmaAllocationCreateFlags allocFlags{ 0 };

		FGBufferHandle handle;
	};

	struct FGTextureInfo
	{
		VkFormat format;
		VkExtent2D extent{ 0, 0 };
		FGResizeMode resizeMode{ FGResizeMode::Fixed };
		uint32_t mipLevels{ 1 };
		VkImageUsageFlags usage{ 0 };

		FGTextureHandle handle;
	};

	struct FGReferenceInfo
	{
		FGResourceHandle handle;
	};

	struct FGResource
	{
		using Info = std::variant<FGBufferInfo, FGTextureInfo, FGReferenceInfo>;

		enum class Usage
		{
			None,
			ColorAttachment,
			DepthStencilAttachment,
			FragmentReadSampled,
			ComputeReadUniform,
			ComputeReadStorage,
			ComputeWriteStorage,
			ComputeReadSampled,
			TransferSrc,
			TransferDst,
			Present,
			IndirectBuffer,
		};

		struct AccessInfo
		{
			VkPipelineStageFlags stage;
			VkAccessFlags access;
			VkImageLayout layout;
		};

		std::string name;
		Usage usage;
		Info info;

		static auto toAccessInfo(Usage usage) -> AccessInfo
		{
			switch (usage)
			{
			default:
				AGX_UNREACHABLE("Unknown FGResource::Usage value");
				[[fallthrough]];
			case Usage::None:
				return AccessInfo{
					.stage = 0,
					.access = 0,
					.layout = VK_IMAGE_LAYOUT_UNDEFINED
				};
			case Usage::ColorAttachment:
				return AccessInfo{
					.stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					.access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
					.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
				};
			case Usage::DepthStencilAttachment:
				return AccessInfo{
					.stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
					.access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
					.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
				};
			case Usage::FragmentReadSampled:
				return AccessInfo{
					.stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					.access = VK_ACCESS_SHADER_READ_BIT,
					.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				};
			case Usage::ComputeReadUniform:
				return AccessInfo{
					.stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					.access = VK_ACCESS_SHADER_READ_BIT,
					.layout = VK_IMAGE_LAYOUT_UNDEFINED
				};
			case Usage::ComputeReadStorage:
				return AccessInfo{
					.stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					.access = VK_ACCESS_SHADER_READ_BIT,
					.layout = VK_IMAGE_LAYOUT_GENERAL
				};
			case Usage::ComputeWriteStorage:
				return AccessInfo{
					.stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					.access = VK_ACCESS_SHADER_WRITE_BIT,
					.layout = VK_IMAGE_LAYOUT_GENERAL
				};
			case Usage::ComputeReadSampled:
				return AccessInfo{
					.stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					.access = VK_ACCESS_SHADER_READ_BIT,
					.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				};
			case Usage::TransferSrc:
				return AccessInfo{
					.stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
					.access = VK_ACCESS_TRANSFER_READ_BIT,
					.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
				};
			case Usage::TransferDst:
				return AccessInfo{
					.stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
					.access = VK_ACCESS_TRANSFER_WRITE_BIT,
					.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
				};
			case Usage::Present:
				return AccessInfo{
					.stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
					.access = 0,
					.layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
				};
			case Usage::IndirectBuffer:
				return AccessInfo{
					.stage = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
					.access = VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
					.layout = VK_IMAGE_LAYOUT_UNDEFINED
				};
			}
		}

		static auto toImageUsage(Usage usage) -> VkImageUsageFlags
		{
			switch (usage)
			{
			case Usage::ColorAttachment:
				return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
			case Usage::DepthStencilAttachment:
				return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			case Usage::FragmentReadSampled:
				return VK_IMAGE_USAGE_SAMPLED_BIT;
			case Usage::ComputeReadStorage:
				return VK_IMAGE_USAGE_STORAGE_BIT;
			case Usage::ComputeWriteStorage:
				return VK_IMAGE_USAGE_STORAGE_BIT;
			case Usage::ComputeReadSampled:
				return VK_IMAGE_USAGE_SAMPLED_BIT;
			case Usage::TransferSrc:
				return VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			case Usage::TransferDst:
				return VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			case Usage::Present:
				return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; // Typically also a color attachment

			case Usage::IndirectBuffer: [[fallthrough]];
			case Usage::ComputeReadUniform:
				AGX_UNREACHABLE("Buffer usage is not applicable for images");
				return 0;

			default:
				AGX_UNREACHABLE("Unknown FGResource::Usage value");
				return 0;
			}
		}

		static auto toBufferUsage(Usage usage) -> VkBufferUsageFlags
		{
			switch (usage)
			{
			case Usage::ComputeReadUniform:
				return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
			case Usage::ComputeReadStorage:
				return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			case Usage::ComputeWriteStorage:
				return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			case Usage::TransferSrc:
				return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			case Usage::TransferDst:
				return VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			case Usage::IndirectBuffer:
				return VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

			case Usage::ColorAttachment: [[fallthrough]];
			case Usage::DepthStencilAttachment: [[fallthrough]];
			case Usage::FragmentReadSampled: [[fallthrough]];
			case Usage::ComputeReadSampled: [[fallthrough]];
			case Usage::Present:
				AGX_UNREACHABLE("Image usage is not applicable for buffers");
				return 0;

			default:
				AGX_UNREACHABLE("Unknown FGResource::Usage value");
				return 0;
			}
		}
	};
}
