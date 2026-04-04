add_library(aegis-core STATIC)

target_link_libraries(aegis-core 
PUBLIC
	aegis-utils
	imguizmo
	Aegis::Log
)

target_sources(aegis-core
PUBLIC 
	FILE_SET CXX_MODULES 
	BASE_DIRS "${AEGIS_MODULE_ROOT}"
	FILES
		asset.cppm
		asset_manager.cppm
		globals.cppm
		input.cppm
		layer.cppm
		layer_stack.cppm
		logging.cppm
		profiler.cppm
		window.cppm
)

target_compile_definitions(aegis-core PUBLIC
    PROJECT_DIR="${PROJECT_DIR}"
    BUILD_DIR="${BUILD_DIR}"
)

# TODO: remove this once everything is a module
target_include_directories(aegis-core PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/..
)
