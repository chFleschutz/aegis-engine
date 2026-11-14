module;
// MSVC needs <compare> wherever a defaulted operator<=> is instantiated, including Vulkan-Hpp's
// vk::Flags reached through aegis.rhi.

export module aegis.assets;
import std;
export import :defaults;
export import :environment;
export import :loader;
export import :material;
export import :static_mesh;
export import :texture;
export import aegis.assets.data;
