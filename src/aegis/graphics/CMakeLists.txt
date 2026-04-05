add_library(aegis-graphics STATIC)

add_subdirectory(vulkan)
add_subdirectory(resources)
add_subdirectory(bindless)
add_subdirectory(material)
add_subdirectory(frame_graph)
add_subdirectory(render_passes)
add_subdirectory(render_systems)
add_subdirectory(loader)

target_link_libraries(aegis-graphics 
PUBLIC
	aegis-core
	aegis-math
	aegis-scene
	aegis-utils
	Aegis::Log
	Aegis::GLTF
	volk::volk
	GPUOpen::VulkanMemoryAllocator
	stb
	glfw
	meshoptimizer
	fastgltf
	tinyobjloader
)

target_sources(aegis-graphics
PUBLIC 
	FILE_SET CXX_MODULES 
	BASE_DIRS "${AEGIS_MODULE_ROOT}"
	FILES
		components.cppm
		deletion_queue.cppm
		descriptors.cppm
		descriptor_pool.cppm
		device.cppm
		draw_batch_registry.cppm
		frustum.cppm
		globals.cppm
		gpu_timer.cppm
		pipeline.cppm
		renderer.cppm
		render_context.cppm
		swap_chain.cppm
)

# TODO: remove this once everything is a module
target_include_directories(aegis-graphics PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/..
)
