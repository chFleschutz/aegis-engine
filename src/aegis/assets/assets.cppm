module;
// MSVC needs <compare> wherever a defaulted operator<=> is instantiated, including Vulkan-Hpp's
// vk::Flags reached through aegis.rhi.

export module aegis.assets;
import std;
export import :asset_database;
export import :asset_id;
export import :asset_key;
export import :asset_ref;
export import :defaults;
export import :engine_content;
export import :environment;
export import :error;
export import :gpu_types;
export import :image_data;
export import :model_data;
export import :loader;
export import :material;
export import :mesh_blob;
export import :mesh_data;
export import :mesh_preprocessor;
export import :sidecar;
export import :static_mesh;
export import :sub_asset_key;
export import :texture;
