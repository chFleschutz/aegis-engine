module;

#include "graphics/vulkan/vulkan_include.h"

#include <glfw/glfw3.h>

#include <array>
#include <set>
#include <unordered_set>

export module Aegis.Graphics.Vulkan.Device;

import Aegis.Core.Window;
import Aegis.Graphics.Vulkan.Tools;
import Aegis.Graphics.DebugUtils;
import Aegis.Graphics.Globals;

export namespace Aegis::Graphics
{
	struct SwapChainSupportDetails
	{
		VkSurfaceCapabilitiesKHR capabilities{};
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> presentModes;
	};

	struct QueueFamilyIndices
	{
		std::optional<uint32_t> graphicsFamily;
		std::optional<uint32_t> presentFamily;
		bool isComplete() const { return graphicsFamily.has_value() && presentFamily.has_value(); }
	};

	struct VulkanFeatures
	{
		VkPhysicalDeviceFeatures2 core{};
		VkPhysicalDeviceVulkan11Features v11{};
		VkPhysicalDeviceVulkan12Features v12{};
		VkPhysicalDeviceVulkan13Features v13{};
		VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderEXT{};
	};

	class VulkanDevice
	{
		friend class VulkanContext;

	public:
		static constexpr uint32_t API_VERSION = VK_API_VERSION_1_3;
		static constexpr auto VALIDATION_LAYERS = std::array{ "VK_LAYER_KHRONOS_validation" };
		static constexpr auto DEVICE_EXTENSIONS = std::array{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };

		VulkanDevice(const VulkanDevice&) = delete;
		VulkanDevice(VulkanDevice&&) = delete;
		~VulkanDevice()
		{
			vkDestroyCommandPool(m_device, m_commandPool, nullptr);
			vmaDestroyAllocator(m_allocator);
			vkDestroyDevice(m_device, nullptr);
			m_debugMessenger.destroy(m_instance);
			vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
			vkDestroyInstance(m_instance, nullptr);
		}

		auto operator=(const VulkanDevice&) -> VulkanDevice & = delete;
		auto operator=(VulkanDevice&&) -> VulkanDevice & = delete;

		operator VkDevice() { return m_device; }

		[[nodiscard]] auto instance() const -> VkInstance { return m_instance; }
		[[nodiscard]] auto physicalDevice() const -> VkPhysicalDevice { return m_physicalDevice; }
		[[nodiscard]] auto device() const -> VkDevice { return m_device; }
		[[nodiscard]] auto allocator() const -> VmaAllocator { return m_allocator; }
		[[nodiscard]] auto commandPool() const -> VkCommandPool { return m_commandPool; }
		[[nodiscard]] auto surface() const -> VkSurfaceKHR { return m_surface; }
		[[nodiscard]] auto graphicsQueue() const -> VkQueue { return m_graphicsQueue; }
		[[nodiscard]] auto presentQueue() const -> VkQueue { return m_presentQueue; }
		[[nodiscard]] auto properties() const -> const VkPhysicalDeviceProperties& { return m_properties; }
		[[nodiscard]] auto features() const -> const VulkanFeatures& { return m_features; }

		void initialize(Core::Window& window)
		{
			createInstance();
			m_debugMessenger.create(m_instance);
			createSurface(window);
			createPhysicalDevice();
			createLogicalDevice();
			createAllocator();
			createCommandPool();
		}

		auto beginSingleTimeCommands() const -> VkCommandBuffer
		{
			VkCommandBufferAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandPool = m_commandPool;
			allocInfo.commandBufferCount = 1;

			VkCommandBuffer commandBuffer;
			vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer);

			VkCommandBufferBeginInfo beginInfo{};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

			vkBeginCommandBuffer(commandBuffer, &beginInfo);
			return commandBuffer;
		}

		void endSingleTimeCommands(VkCommandBuffer commandBuffer) const
		{
			vkEndCommandBuffer(commandBuffer);

			VkSubmitInfo submitInfo{};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &commandBuffer;

			vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
			vkQueueWaitIdle(m_graphicsQueue);

			vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
		}

		void createBuffer(VkBuffer& buffer, VmaAllocation& allocation, VkDeviceSize size, VkBufferUsageFlags bufferUsage, VmaAllocationCreateFlags allocFlags, VmaMemoryUsage memoryUsage) const
		{
			VkBufferCreateInfo bufferInfo{};
			bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufferInfo.size = size;
			bufferInfo.usage = bufferUsage;
			bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VmaAllocationCreateInfo allocInfo{};
			allocInfo.usage = memoryUsage;
			allocInfo.flags = allocFlags;

			VK_CHECK(vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr));
		}

		void createImage(VkImage& image, VmaAllocation& allocation, const VkImageCreateInfo& imageInfo, const VmaAllocationCreateInfo& allocInfo) const
		{
			VK_CHECK(vmaCreateImage(m_allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr));
		}

		auto querySwapChainSupport() const -> SwapChainSupportDetails
		{
			return querySwapChainSupport(m_physicalDevice);
		}

		auto findPhysicalQueueFamilies() const -> QueueFamilyIndices
		{
			return findQueueFamilies(m_physicalDevice);
		}

		auto findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) const -> VkFormat
		{
			for (VkFormat format : candidates)
			{
				VkFormatProperties props;
				vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &props);

				if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
				{
					return format;
				}
				else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
				{
					return format;
				}
			}

			AGX_ASSERT_X(false, "failed to find supported format!");
			return VK_FORMAT_UNDEFINED;
		}

		auto findAspectFlags(VkFormat format) const -> VkImageAspectFlags
		{
			switch (format)
			{
			case VK_FORMAT_D16_UNORM:
			case VK_FORMAT_X8_D24_UNORM_PACK32:
			case VK_FORMAT_D32_SFLOAT:
				return VK_IMAGE_ASPECT_DEPTH_BIT;

			case VK_FORMAT_S8_UINT:
				return VK_IMAGE_ASPECT_STENCIL_BIT;

			case VK_FORMAT_D16_UNORM_S8_UINT:
			case VK_FORMAT_D24_UNORM_S8_UINT:
			case VK_FORMAT_D32_SFLOAT_S8_UINT:
				return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

			default:
				return VK_IMAGE_ASPECT_COLOR_BIT;  // For color formats
			}
		}

		void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) const
		{
			VkCommandBuffer commandBuffer = beginSingleTimeCommands();

			VkBufferCopy copyRegion{};
			copyRegion.srcOffset = 0;
			copyRegion.dstOffset = 0;
			copyRegion.size = size;
			vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

			endSingleTimeCommands(commandBuffer);
		}

		void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, uint32_t layerCount) const
		{
			VkCommandBuffer commandBuffer = beginSingleTimeCommands();

			VkBufferImageCopy region{};
			region.bufferOffset = 0;
			region.bufferRowLength = 0;
			region.bufferImageHeight = 0;
			region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.mipLevel = 0;
			region.imageSubresource.baseArrayLayer = 0;
			region.imageSubresource.layerCount = layerCount;
			region.imageOffset = { 0, 0, 0 };
			region.imageExtent = { width, height, 1 };

			vkCmdCopyBufferToImage(commandBuffer,
				buffer,
				image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&region);

			endSingleTimeCommands(commandBuffer);
		}

		void transitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
			uint32_t mipLevels = 1, VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT) const
		{
			VkImageMemoryBarrier barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.oldLayout = oldLayout;
			barrier.newLayout = newLayout;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = image;
			barrier.subresourceRange.aspectMask = aspectFlags;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = mipLevels;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;
			barrier.srcAccessMask = Tools::srcAccessMask(barrier.oldLayout);
			barrier.dstAccessMask = Tools::dstAccessMask(barrier.newLayout);

			VkPipelineStageFlags srcStage = Tools::srcStage(barrier.srcAccessMask);
			VkPipelineStageFlags dstStage = Tools::dstStage(barrier.dstAccessMask);

			vkCmdPipelineBarrier(commandBuffer,
				srcStage, dstStage,
				0,
				0, nullptr,
				0, nullptr,
				1, &barrier
			);
		}

		void transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels = 1,
			VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT) const
		{
			VkCommandBuffer commandBuffer = beginSingleTimeCommands();
			transitionImageLayout(commandBuffer, image, oldLayout, newLayout, mipLevels, aspectFlags);
			endSingleTimeCommands(commandBuffer);
		}

		void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout,
			uint32_t mipLevels = 1) const
		{
			transitionImageLayout(image, oldLayout, newLayout, mipLevels, findAspectFlags(format));
		}

	private:
		VulkanDevice() = default;

		void createInstance()
		{
			VK_CHECK(volkInitialize());

			AGX_ASSERT_X(!ENABLE_VALIDATION || checkValidationLayerSupport(), "Validation layers requested, but not available!");

			VkApplicationInfo appInfo{};
			appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
			appInfo.pApplicationName = "Aegis App";
			appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
			appInfo.pEngineName = "Aegis Engine";
			appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
			appInfo.apiVersion = API_VERSION;

			auto extensions = queryRequiredInstanceExtensions();

			VkInstanceCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
			createInfo.pApplicationInfo = &appInfo;
			createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
			createInfo.ppEnabledExtensionNames = extensions.data();
			createInfo.enabledLayerCount = 0;
			createInfo.pNext = nullptr;

			if constexpr (ENABLE_VALIDATION)
			{
				createInfo.enabledLayerCount = static_cast<uint32_t>(VALIDATION_LAYERS.size());
				createInfo.ppEnabledLayerNames = VALIDATION_LAYERS.data();
				
				VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = DebugUtilsMessenger::populateCreateInfo();
				createInfo.pNext = &debugCreateInfo;
				ALOG::info("Vulkan Validation Layer enabled");
			}

			VK_CHECK(vkCreateInstance(&createInfo, nullptr, &m_instance));
			volkLoadInstance(m_instance);

			checkGflwRequiredInstanceExtensions();
		}

		void createSurface(Core::Window& window)
		{
			window.createSurface(m_instance, m_surface);
		}

		void createPhysicalDevice()
		{
			uint32_t deviceCount = 0;
			vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

			AGX_ASSERT_X(deviceCount > 0, "Failed to find GPUs with Vulkan support");
			ALOG::info("Available GPUs: {}", deviceCount);

			std::vector<VkPhysicalDevice> devices(deviceCount);
			vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

			// Search for a suitable device
			for (const auto& device : devices)
			{
				QueueFamilyIndices indices = findQueueFamilies(device);
				if (!indices.isComplete())
					continue;

				if (!checkDeviceExtensionSupport(device))
					continue;

				if (!checkDeviceFeatureSupport(device))
					continue;

				SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
				if (swapChainSupport.formats.empty() || swapChainSupport.presentModes.empty())
					continue;

				// Found a suitable device
				m_physicalDevice = device;
				break;
			}

			AGX_ASSERT_X(m_physicalDevice != VK_NULL_HANDLE, "Failed to find a suitable GPU");

			vkGetPhysicalDeviceProperties(m_physicalDevice, &m_properties);
			uint32_t major = VK_VERSION_MAJOR(m_properties.apiVersion);
			uint32_t minor = VK_VERSION_MINOR(m_properties.apiVersion);
			uint32_t patch = VK_VERSION_PATCH(m_properties.apiVersion);
			ALOG::info("{} (Vulkan {}.{}.{})", m_properties.deviceName, major, minor, patch);

			// Query features
			m_features = {};
			m_features.core.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
			m_features.core.pNext = &m_features.v11;
			m_features.v11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
			m_features.v11.pNext = &m_features.v12;
			m_features.v12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
			m_features.v12.pNext = &m_features.v13;
			m_features.v13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
			m_features.v13.pNext = &m_features.meshShaderEXT;
			m_features.meshShaderEXT.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;

			vkGetPhysicalDeviceFeatures2(m_physicalDevice, &m_features.core);

			if (!m_features.meshShaderEXT.meshShader || !m_features.meshShaderEXT.taskShader)
				ALOG::warn("Mesh shaders not supported");
		}

		void createLogicalDevice()
		{
			// IMPORTANT NOTE
			// When enabling new features, make sure to check for their support in checkDeviceFeatureSupport()

			// TODO: Make feature enabling more easy to configure

			VkPhysicalDeviceFeatures2 features{
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
				.features = VkPhysicalDeviceFeatures{
						.samplerAnisotropy = VK_TRUE,
					},
			};

			VkPhysicalDeviceVulkan11Features vulkan11Features{
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
				.shaderDrawParameters = VK_TRUE,
			};

			VkPhysicalDeviceVulkan12Features vulkan12Features{
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
				// 8-bit storage
				.storageBuffer8BitAccess = VK_TRUE,
				.uniformAndStorageBuffer8BitAccess = VK_TRUE,
				.storagePushConstant8 = VK_TRUE,
				.shaderInt8 = VK_TRUE,
				// Bindless descriptor sets
				.descriptorIndexing = VK_TRUE,
				.shaderUniformBufferArrayNonUniformIndexing = VK_TRUE,
				.shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
				.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE,
				.shaderStorageImageArrayNonUniformIndexing = VK_TRUE,
				.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE,
				.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
				.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE,
				.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE,
				.descriptorBindingUpdateUnusedWhilePending = VK_TRUE,
				.descriptorBindingPartiallyBound = VK_TRUE,
				.descriptorBindingVariableDescriptorCount = VK_TRUE,
				.runtimeDescriptorArray = VK_TRUE,
				// Misc
				.scalarBlockLayout = VK_TRUE,
				.uniformBufferStandardLayout = VK_TRUE,
			};

			VkPhysicalDeviceVulkan13Features vulkan13Features{
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
				.shaderDemoteToHelperInvocation = VK_TRUE, // SPIRV 1.6 requirement for using 'discard'
				.dynamicRendering = VK_TRUE,
				.maintenance4 = VK_TRUE,
			};

			VkPhysicalDeviceMeshShaderFeaturesEXT meshShader{
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT,
				.taskShader = m_features.meshShaderEXT.taskShader,
				.meshShader = m_features.meshShaderEXT.meshShader,
			};

			// Build the pNext chain
			features.pNext = &vulkan11Features;
			vulkan11Features.pNext = &vulkan12Features;
			vulkan12Features.pNext = &vulkan13Features;
			vulkan13Features.pNext = nullptr;

			std::vector<const char*> enabledExtensions(DEVICE_EXTENSIONS.begin(), DEVICE_EXTENSIONS.end());
			if (meshShader.taskShader && meshShader.meshShader)
			{
				vulkan13Features.pNext = &meshShader;
				enabledExtensions.emplace_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
			}

			QueueFamilyIndices indices = findQueueFamilies(m_physicalDevice);
			AGX_ASSERT_X(indices.isComplete(), "Queue family indices are not complete");

			std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
			std::set<uint32_t> uniqueQueueFamilies = { indices.graphicsFamily.value(), indices.presentFamily.value() };

			float queuePriority = 1.0f;
			for (uint32_t queueFamily : uniqueQueueFamilies)
			{
				VkDeviceQueueCreateInfo queueCreateInfo{};
				queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queueCreateInfo.queueFamilyIndex = queueFamily;
				queueCreateInfo.queueCount = 1;
				queueCreateInfo.pQueuePriorities = &queuePriority;
				queueCreateInfos.push_back(queueCreateInfo);
			}

			VkDeviceCreateInfo createInfo{
				.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
				.pNext = &features,
				.flags = 0,
				.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size()),
				.pQueueCreateInfos = queueCreateInfos.data(),
				.enabledLayerCount = 0,
				.ppEnabledLayerNames = nullptr,
				.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size()),
				.ppEnabledExtensionNames = enabledExtensions.data(),
				.pEnabledFeatures = nullptr,
			};

			// might not really be necessary anymore because device specific validation layers have been deprecated
			if constexpr (ENABLE_VALIDATION)
			{
				createInfo.enabledLayerCount = static_cast<uint32_t>(VALIDATION_LAYERS.size());
				createInfo.ppEnabledLayerNames = VALIDATION_LAYERS.data();
			}

			VK_CHECK(vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device));
			volkLoadDevice(m_device);

			AGX_ASSERT_X(indices.isComplete(), "Queue family indices are not complete");
			vkGetDeviceQueue(m_device, indices.graphicsFamily.value(), 0, &m_graphicsQueue);
			vkGetDeviceQueue(m_device, indices.presentFamily.value(), 0, &m_presentQueue);
		}

		void createAllocator()
		{
			VmaAllocatorCreateInfo allocatorInfo{
				.physicalDevice = m_physicalDevice,
				.device = m_device,
				.instance = m_instance,
				.vulkanApiVersion = API_VERSION
			};

			VmaVulkanFunctions vulkanFunctions;
			vmaImportVulkanFunctionsFromVolk(&allocatorInfo, &vulkanFunctions);
			allocatorInfo.pVulkanFunctions = &vulkanFunctions;

			VK_CHECK(vmaCreateAllocator(&allocatorInfo, &m_allocator));
		}

		void createCommandPool()
		{
			QueueFamilyIndices queueFamilyIndices = findPhysicalQueueFamilies();
			AGX_ASSERT_X(queueFamilyIndices.graphicsFamily.has_value(), "Graphics queue family not found");

			VkCommandPoolCreateInfo poolInfo = {};
			poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
			poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

			VK_CHECK(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool));
		}

		auto queryRequiredInstanceExtensions() const -> std::vector<const char*>
		{
			uint32_t glfwExtensionCount = 0;
			const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

			std::vector<const char*> extensions;
			extensions.reserve(glfwExtensionCount);
			extensions.insert(extensions.end(), glfwExtensions, glfwExtensions + glfwExtensionCount);

			if constexpr (ENABLE_VALIDATION)
			{
				extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
			}

			return extensions;
		}

		auto checkValidationLayerSupport() -> bool
		{
			uint32_t layerCount;
			vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

			std::vector<VkLayerProperties> availableLayers(layerCount);
			vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

			for (auto layerName : VALIDATION_LAYERS)
			{
				bool layerFound = false;
				for (const auto& layerProperties : availableLayers)
				{
					if (strcmp(layerName, layerProperties.layerName) == 0)
					{
						layerFound = true;
						break;
					}
				}

				if (!layerFound)
					return false;
			}

			return true;
		}

		auto findQueueFamilies(VkPhysicalDevice device) const -> QueueFamilyIndices
		{
			QueueFamilyIndices indices;

			uint32_t queueFamilyCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

			std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
			vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

			int i = 0;
			for (const auto& queueFamily : queueFamilies)
			{
				if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
					indices.graphicsFamily = i;

				VkBool32 presentSupport = false;
				vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
				if (queueFamily.queueCount > 0 && presentSupport)
					indices.presentFamily = i;

				if (indices.isComplete())
					break;

				i++;
			}

			return indices;
		}

		void checkGflwRequiredInstanceExtensions()
		{
			uint32_t extensionCount = 0;
			vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
			std::vector<VkExtensionProperties> extensions(extensionCount);
			vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());

			std::unordered_set<std::string> available;
			for (const auto& extension : extensions)
			{
				available.insert(extension.extensionName);
			}

			for (const auto& required : queryRequiredInstanceExtensions())
			{
				AGX_ASSERT_X(available.find(required) != available.end(), "Missing required glfw extension");
			}
		}

		auto checkDeviceExtensionSupport(VkPhysicalDevice device) -> bool
		{
			uint32_t extensionCount;
			vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

			std::vector<VkExtensionProperties> availableExtensions(extensionCount);
			vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

			std::set<std::string> requiredExtensions(DEVICE_EXTENSIONS.begin(), DEVICE_EXTENSIONS.end());
			for (const auto& extension : availableExtensions)
			{
				requiredExtensions.erase(extension.extensionName);
			}

			return requiredExtensions.empty();
		}

		auto checkDeviceFeatureSupport(VkPhysicalDevice device) -> bool
		{
			VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures{
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT,
				.pNext = nullptr,
			};

			VkPhysicalDeviceVulkan13Features vulkan13Features{
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
				.pNext = &meshShaderFeatures,
			};

			VkPhysicalDeviceVulkan12Features vulkan12Features{
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
				.pNext = &vulkan13Features,
			};

			VkPhysicalDeviceVulkan11Features vulkan11Features{
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
				.pNext = &vulkan12Features,
			};

			VkPhysicalDeviceFeatures2 vulkan10{
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
				.pNext = &vulkan11Features,
			};

			vkGetPhysicalDeviceFeatures2(device, &vulkan10);

			if (!vulkan10.features.samplerAnisotropy)
				return false;

			if (!vulkan11Features.shaderDrawParameters)
				return false;

			// Bindless
			if (!vulkan12Features.descriptorIndexing ||
				!vulkan12Features.shaderUniformBufferArrayNonUniformIndexing ||
				!vulkan12Features.shaderSampledImageArrayNonUniformIndexing ||
				!vulkan12Features.shaderStorageBufferArrayNonUniformIndexing ||
				!vulkan12Features.shaderStorageImageArrayNonUniformIndexing ||
				!vulkan12Features.descriptorBindingUniformBufferUpdateAfterBind ||
				!vulkan12Features.descriptorBindingSampledImageUpdateAfterBind ||
				!vulkan12Features.descriptorBindingStorageImageUpdateAfterBind ||
				!vulkan12Features.descriptorBindingStorageBufferUpdateAfterBind ||
				!vulkan12Features.descriptorBindingPartiallyBound ||
				!vulkan12Features.descriptorBindingVariableDescriptorCount ||
				!vulkan12Features.runtimeDescriptorArray)
				return false;

			// Buffer layouts 
			if (!vulkan12Features.uniformBufferStandardLayout ||
				!vulkan12Features.scalarBlockLayout)
				return false;

			// 8-bit storage
			if (!vulkan12Features.shaderInt8 ||
				!vulkan12Features.storageBuffer8BitAccess ||
				!vulkan12Features.uniformAndStorageBuffer8BitAccess ||
				!vulkan12Features.storagePushConstant8)
				return false;

			if (!vulkan13Features.dynamicRendering ||
				!vulkan13Features.maintenance4 ||
				!vulkan13Features.shaderDemoteToHelperInvocation)
				return false;

			// TODO: Add mesh shader as optional feature
			if (!meshShaderFeatures.meshShader || !meshShaderFeatures.taskShader)
				return false;

			m_features = {
				.core = vulkan10,
				.v11 = vulkan11Features,
				.v12 = vulkan12Features,
				.v13 = vulkan13Features,
				.meshShaderEXT = meshShaderFeatures,
			};

			return true;
		}

		auto querySwapChainSupport(VkPhysicalDevice device) const -> SwapChainSupportDetails
		{
			SwapChainSupportDetails details;
			vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &details.capabilities);

			uint32_t formatCount;
			vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);

			if (formatCount != 0)
			{
				details.formats.resize(formatCount);
				vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, details.formats.data());
			}

			uint32_t presentModeCount;
			vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);

			if (presentModeCount != 0)
			{
				details.presentModes.resize(presentModeCount);
				vkGetPhysicalDeviceSurfacePresentModesKHR(
					device,
					m_surface,
					&presentModeCount,
					details.presentModes.data());
			}
			return details;
		}

		VkInstance m_instance = VK_NULL_HANDLE;
		VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
		VkDevice m_device = VK_NULL_HANDLE;
		VmaAllocator m_allocator = VK_NULL_HANDLE;
		VkCommandPool m_commandPool = VK_NULL_HANDLE;

		DebugUtilsMessenger m_debugMessenger;

		VkPhysicalDeviceProperties m_properties{};
		VulkanFeatures m_features{};

		VkSurfaceKHR m_surface = VK_NULL_HANDLE;
		VkQueue m_graphicsQueue = VK_NULL_HANDLE;
		VkQueue m_presentQueue = VK_NULL_HANDLE;
	};
}
