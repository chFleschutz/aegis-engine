# The asset library: loads assets by ID on a device. Its own target so aegis-test can check it without the engine.
add_library(aegis-asset-library STATIC)
target_link_libraries(aegis-asset-library
PUBLIC
	aegis-assets
	aegis-reflect
	aegis-rhi
	aegis-serialize # AssetLibraryError carries serialize::Error
	aegis-world
PRIVATE
	aegis-core
	aegis-paths
	# Bakes environments with Ibl
	aegis-render-passes
)
target_sources(aegis-asset-library
PUBLIC
	FILE_SET CXX_MODULES FILES
		asset_library.cppm
		asset_library_error.cppm
PRIVATE
	asset_library.cpp
)
