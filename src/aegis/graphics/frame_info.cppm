module;

#include "graphics/vulkan/vulkan_include.h"

export module Aegis.Graphics.FrameInfo;

import Aegis.Graphics.DrawBatchRegistry;
import Aegis.Scene;
import Aegis.UI;

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
