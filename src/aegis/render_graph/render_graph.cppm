module;
// MSVC needs <compare> wherever a defaulted operator<=> is instantiated, including Vulkan-Hpp's
// vk::Flags reached through aegis.rhi.

export module aegis.render_graph;
import std;
export import :declarations;
export import :default_graph;
export import :error;
export import :frame_graph;
export import :frame_scene;
export import :frustum;
export import :geometry_pass;
export import :gpu_driven;
export import :gpu_types;
export import :graph_builder;
export import :ibl;
export import :instance_store;
export import :immediate;
export import :lighting_pass;
export import :names;
export import :post_effects;
export import :render_pass;
export import :render_system;
export import :resource;
export import :resource_pool;
export import :scene_update_pass;
export import :schedule;
export import :screen_passes;
export import :shader;
export import :static_mesh_render_system;
export import :ui_pass;
