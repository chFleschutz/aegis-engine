module;

#include "graphics/bindless/descriptor_handle.h"
#include "graphics/vulkan/volk_include.h"
#include "scene/scene.h"
#include "ui/ui.h"

export module Aegis.Graphics.RenderContext;

export namespace Aegis::Graphics
{
	struct RenderContext
	{
		Scene::Scene& scene;
		UI::UI& ui;
		uint32_t frameIndex{ 0 };
		VkCommandBuffer cmd{ VK_NULL_HANDLE };
		VkDescriptorSet globalSet{ VK_NULL_HANDLE };
		DescriptorHandle globalHandle;
	};
}