set(AEGIS_MODULE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(aegis-engine STATIC)
add_library(Aegis::Engine ALIAS aegis-engine)

add_subdirectory(math)
add_subdirectory(core)
add_subdirectory(graphics)
add_subdirectory(physics)
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
#	Aegis::GLTF
#	Aegis::Log
#	EnTT::EnTT
#	fastgltf::fastgltf
#	glfw
#	glm::glm
#	imgui
#	imguizmo
#	meshoptimizer
#	stb
#	tinyobjloader
#	GPUOpen::VulkanMemoryAllocator
#	volk::volk_headers
)

target_sources(aegis-engine 
PUBLIC 
	FILE_SET CXX_MODULES 
	BASE_DIRS "${AEGIS_MODULE_ROOT}"
	FILES
		engine.cppm
		scene_defaults.cppm
)
