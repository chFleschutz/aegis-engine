set(CMAKE_CXX_SCAN_FOR_MODULES ON)
set(AEGIS_MODULE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(aegis-engine STATIC)
add_library(Aegis::Engine ALIAS aegis-engine)

add_subdirectory(math)
add_subdirectory(core)
add_subdirectory(rhi)
add_subdirectory(graphics)
add_subdirectory(scene)
add_subdirectory(scripting)
add_subdirectory(ui)
add_subdirectory(utils)
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
	aegis-core
	aegis-graphics
	aegis-scene
	aegis-ui
	aegis-editor
	aegis-scripting
)

target_sources(aegis-engine 
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
