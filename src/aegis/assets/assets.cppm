module;
// MSVC needs <compare> wherever a defaulted operator<=> is instantiated, including Vulkan-Hpp's
// vk::Flags reached through aegis.rhi.

export module aegis.assets;
import std;
export import :components;
export import :defaults;
export import :error;
export import :loader;
export import :material;
export import :mesh_data;
export import :mesh_preprocessor;
export import :static_mesh;
export import :texture;
