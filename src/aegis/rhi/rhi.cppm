module;
// MSVC needs <compare> wherever a defaulted operator<=> is instantiated, including Vulkan-Hpp's
// vk::Flags reached through the partitions below.

export module aegis.rhi;
import std;
export import :bindless_heap;
export import :buffer;
export import :command_buffer;
export import :command_pool;
export import :commands;
export import :common;
export import :context;
export import :deletion_queue;
export import :device;
export import :error;
export import :image;
export import :image_view;
export import :pipeline;
export import :query;
export import :queue;
export import :resource_handle;
export import :resource_pool;
export import :ring_allocator;
export import :sampler;
export import :swapchain;
export import :sync;
export import :upload_manager;
export import :utility;
