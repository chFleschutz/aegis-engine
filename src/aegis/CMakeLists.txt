set(CMAKE_CXX_SCAN_FOR_MODULES ON)
set(AEGIS_MODULE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# The asset library: loads assets by ID on a device. Its own target so aegis-test can check it without the engine.
add_library(aegis-asset-library STATIC)
target_link_libraries(aegis-asset-library
PUBLIC
	aegis-assets
	aegis-reflect
	aegis-render-graph
	aegis-rhi
	aegis-world
PRIVATE
	aegis-base
	aegis-instantiate
	aegis-paths
	aegis-serialize
)
target_sources(aegis-asset-library
PUBLIC
	FILE_SET CXX_MODULES FILES
		asset_library.cppm
PRIVATE
	asset_library.cpp
)

add_library(aegis-engine STATIC)
add_library(Aegis::Engine ALIAS aegis-engine)

add_subdirectory(base)
add_subdirectory(paths)
add_subdirectory(platform)
add_subdirectory(rhi)
add_subdirectory(renderer)
add_subdirectory(assets)
add_subdirectory(instantiate)
add_subdirectory(reflect)
add_subdirectory(render_graph)
add_subdirectory(test)

add_subdirectory(math)
add_subdirectory(core)
add_subdirectory(world)
add_subdirectory(scripting)
add_subdirectory(serialize)
add_subdirectory(ui)
add_subdirectory(editor)

target_include_directories(aegis-engine 
PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/..
	${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(aegis-engine 
PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(aegis-engine 
PUBLIC 
	aegis-asset-library
	aegis-assets
	aegis-base
	aegis-core
	aegis-instantiate
	aegis-paths
	aegis-platform
	aegis-reflect
	aegis-render-graph
	aegis-renderer
	aegis-rhi
	aegis-world
	aegis-ui
	aegis-editor
	aegis-scripting
)

target_link_libraries(aegis-engine
PRIVATE
	stb # stb_image_write for the headless screenshot
)

target_sources(aegis-engine
PRIVATE
	engine.cpp
PUBLIC 
	FILE_SET CXX_MODULES 
	FILES
		engine.cppm
		scene_defaults.cppm
		scene_description.cppm
)

if (UNIX AND NOT APPLE)
	find_package(X11 REQUIRED)
	target_link_libraries(aegis-engine PRIVATE ${X11_LIBRARIES})
endif()
