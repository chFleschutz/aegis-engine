module;

#include "graphics/vulkan/vulkan_include.h"

export module Aegis.Graphics.RenderContext;

import Aegis.Scene;
import Aegis.UI;
import Aegis.Graphics.Bindless.DescriptorHandle;

export namespace Aegis::Graphics
{
	struct RenderContext
	{
		Scene::Scene& scene;
		UI::UI& ui;
		uint32_t frameIndex{ 0 };
		VkCommandBuffer cmd{ VK_NULL_HANDLE };
		VkDescriptorSet globalSet{ VK_NULL_HANDLE };
		Bindless::DescriptorHandle globalHandle;
	};
}