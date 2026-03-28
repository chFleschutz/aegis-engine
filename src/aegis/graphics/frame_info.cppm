module;

#include "graphics/draw_batch_registry.h"
#include "graphics/vulkan/volk_include.h"
#include "scene/scene.h"
#include "ui/ui.h"

export module Aegis.Graphics.FrameInfo;

export namespace Aegis::Graphics
{
	struct FrameInfo
	{
		Scene::Scene& scene;
		UI::UI& ui;
		DrawBatchRegistry& drawBatcher;
		VkCommandBuffer cmd{ VK_NULL_HANDLE };
		uint32_t frameIndex{ 0 };
		VkExtent2D swapChainExtent;
		float aspectRatio;
	};
}
