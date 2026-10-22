module;
// MSVC needs <compare> wherever a defaulted operator<=> is instantiated, including Vulkan-Hpp's
// vk::Flags reached through aegis.rhi.

// The engine's render passes, IBL and the shader loader, built on the aegis.render_graph framework
// (re-exported). They share its namespace, aegis::render_graph.
export module aegis.render_passes;
import std;
export import aegis.render_graph;
export import :default_graph;
export import :geometry_pass;
export import :gpu_driven;
export import :gpu_types;
export import :ibl;
export import :lighting_pass;
export import :names;
export import :post_effects;
export import :render_system;
export import :scene_update_pass;
export import :screen_passes;
export import :shader;
export import :static_mesh_render_system;
export import :ui_pass;
