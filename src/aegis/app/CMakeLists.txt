# The asset library: loads assets by ID on a device. Its own target so aegis-test can check it without the engine.
add_library(aegis-asset-library STATIC)
target_link_libraries(aegis-asset-library
PUBLIC
	aegis-assets
	aegis-reflect
	aegis-rhi
	aegis-world
PRIVATE
	aegis-core
	aegis-paths
	aegis-render-passes # Bakes environments with Ibl
	aegis-serialize
)
target_sources(aegis-asset-library
PUBLIC
	FILE_SET CXX_MODULES FILES
		asset_library.cppm
PRIVATE
	asset_library.cpp
)

# Between the world and the render graph: the instance store that follows the world, the per-frame FrameScene, and
# the worlds an app holds, each with its own store. Its own target so aegis-test renders a world through it without
# the engine.
add_library(aegis-render-bridge STATIC)
target_link_libraries(aegis-render-bridge
PUBLIC
	aegis-render-passes # FrameScene and InstanceStore
	aegis-world
	EnTT::EnTT # Follows the world's component signals
PRIVATE
	aegis-assets
	aegis-core
	aegis-math
	aegis-rhi
)
target_sources(aegis-render-bridge
PUBLIC
	FILE_SET CXX_MODULES FILES
		render_bridge.cppm
PRIVATE
	render_bridge.cpp
)

add_library(aegis-engine STATIC)
add_library(Aegis::Engine ALIAS aegis-engine)

target_include_directories(aegis-engine 
PUBLIC
	${AEGIS_MODULE_ROOT}/..
	${AEGIS_MODULE_ROOT}
)

target_include_directories(aegis-engine 
PRIVATE
	${AEGIS_MODULE_ROOT}/..
)

target_link_libraries(aegis-engine 
PUBLIC 
	aegis-asset-library
	aegis-assets
	aegis-core
	aegis-math # aegis.game re-exports it
	aegis-paths # The launcher (launch/main.cpp, compiled into each game) locates the project
	aegis-platform
	aegis-reflect
	aegis-render-bridge
	aegis-render-graph
	aegis-render-passes # GameModule::buildGraph and Shell::buildGraph declare passes
	aegis-renderer
	aegis-rhi
	aegis-world
	aegis-scripting
	aegis-serialize
)

target_link_libraries(aegis-engine
PRIVATE
	imgui::imgui # Presentation: the ImGui context and its GLFW backend
	stb # stb_image_write for the headless screenshot
)

target_sources(aegis-engine
PRIVATE
	engine.cpp
	presentation.cpp
	scene_defaults.cpp
	scene_description.cpp
PUBLIC 
	FILE_SET CXX_MODULES 
	FILES
		engine.cppm
		game.cppm
		scene_defaults.cppm
		scene_description.cppm
)

if (UNIX AND NOT APPLE)
	find_package(X11 REQUIRED)
	target_link_libraries(aegis-engine PRIVATE ${X11_LIBRARIES})
endif()
