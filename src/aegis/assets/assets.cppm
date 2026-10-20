module;
// MSVC needs <compare> wherever a defaulted operator<=> is instantiated, including Vulkan-Hpp's
// vk::Flags reached through aegis.rhi.

export module aegis.assets;
import std;
export import :asset_key;
export import :defaults;
export import :environment;
export import :error;
export import :gpu_types;
export import :image_data;
export import :model_data;
export import :loader;
export import :material;
export import :mesh_data;
export import :mesh_preprocessor;
export import :static_mesh;
export import :texture;
