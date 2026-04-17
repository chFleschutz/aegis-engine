module;

#include "graphics/vulkan/vulkan_include.h"

#include <aegis-log/log.h>

#include <string_view>

export module Aegis.Graphics.DebugUtils;

import Aegis.Graphics.Globals;

export namespace Aegis::Graphics
{
	class DebugUtilsMessenger
	{
	public:
		DebugUtilsMessenger() = default;
		~DebugUtilsMessenger() = default;

		void create(VkInstance instance)
		{
			if constexpr (ENABLE_VALIDATION)
			{
				VkDebugUtilsMessengerCreateInfoEXT createInfo = populateCreateInfo();
				VK_CHECK(vkCreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &m_debugMessenger));
			}
		}

		void destroy(VkInstance instance)
		{
			if (ENABLE_VALIDATION)
			{
				vkDestroyDebugUtilsMessengerEXT(instance, m_debugMessenger, nullptr);
			}
		}

		static auto debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
			VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
			void* pUserData) -> VkBool32
		{
			ALOG::log(convertSeverity(messageSeverity), "Vulkan {} Error: \n{}\n",
				convertMessageType(messageType), pCallbackData->pMessage);
			return VK_FALSE;
		}

		static auto populateCreateInfo() -> VkDebugUtilsMessengerCreateInfoEXT
		{
			VkDebugUtilsMessengerCreateInfoEXT info{
				.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
				.messageSeverity =
					VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
				.messageType =
					VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
				.pfnUserCallback = debugCallback,
			};

			return info;
		}

		static auto convertSeverity(VkDebugUtilsMessageSeverityFlagBitsEXT severity) -> ALOG::Severity
		{
			switch (severity)
			{
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
				return ALOG::Severity::Trace;
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
				return ALOG::Severity::Info;
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
				return ALOG::Severity::Warn;
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
				return ALOG::Severity::Fatal;
			default:
				return ALOG::Severity::Info;
			}
		}

		static auto convertMessageType(VkDebugUtilsMessageTypeFlagsEXT messageType) -> std::string_view
		{
			switch (messageType)
			{
			case VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT:
				return "General";
			case VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT:
				return "Validation";
			case VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT:
				return "Performance";
			default:
				return "Unknown";
			}
		}

	private:
		VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
	};
}
