// The CPU side of aegis.assets: asset identity, the asset database, sidecars, importers, the import cache and the
// data they produce. It imports aegis.rhi.types but not aegis.rhi, so none of it is rebuilt when an rhi interface
// changes, and nothing here may import aegis.rhi. aegis.assets re-exports it and adds what needs a device.
export module aegis.assets.data;
export import :asset_database;
export import :audio_clip;
export import :asset_id;
export import :asset_key;
export import :asset_ref;
export import :embedded_images;
export import :engine_content;
export import :error;
export import :external_images;
export import :gpu_types;
export import :image_data;
export import :import_cache;
export import :mesh_blob;
export import :mesh_data;
export import :mesh_preprocessor;
export import :model_data;
export import :sidecar;
export import :sub_asset_key;
export import :texture_blob;
export import :texture_data;
