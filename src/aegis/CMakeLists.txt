add_library(aegis-engine STATIC)
add_library(Aegis::Engine ALIAS aegis-engine)

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
		aegis-math
		aegis-utils
		Aegis::GLTF
		Aegis::Log
		EnTT::EnTT
		fastgltf::fastgltf
		glfw
		glm::glm
		imgui
		imguizmo
		meshoptimizer
		stb
		tinyobjloader
		GPUOpen::VulkanMemoryAllocator
		volk::volk_headers
)

target_precompile_headers(aegis-engine PUBLIC pch.h)

add_subdirectory(core)
add_subdirectory(graphics)
add_subdirectory(math)
add_subdirectory(physics)
add_subdirectory(scene)
add_subdirectory(scripting)
add_subdirectory(ui)
add_subdirectory(utils)

target_sources(aegis-engine 
PRIVATE
	engine.cpp
	engine.h
)
