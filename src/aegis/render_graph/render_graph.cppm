module;
// MSVC needs <compare> wherever a defaulted operator<=> is instantiated, including Vulkan-Hpp's
// vk::Flags reached through aegis.rhi.

// The framework: declarations, scheduling, the resource pool and the frame graph. The passes that
// draw with it, IBL and the shader loader are aegis.render_passes.
export module aegis.render_graph;
import std;
export import :declarations;
export import :error;
export import :frame_graph;
export import :frame_scene;
export import :frustum;
export import :graph_builder;
export import :instance_store;
export import :immediate;
export import :render_pass;
export import :resource;
export import :resource_pool;
export import :schedule;
