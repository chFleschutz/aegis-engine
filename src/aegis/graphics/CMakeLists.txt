add_library(aegis-graphics STATIC)

add_subdirectory(vulkan)
add_subdirectory(resources)
add_subdirectory(bindless)
add_subdirectory(material)
add_subdirectory(frame_graph)
add_subdirectory(render_passes)
add_subdirectory(render_systems)

# TODO: remove this once everything is a module
target_include_directories(aegis-graphics PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/..
)

# TODO: remove this once everything is a module
target_link_libraries(aegis-graphics 
PRIVATE
	aegis-math
	aegis-utils
	Aegis::Log
	volk::volk
	GPUOpen::VulkanMemoryAllocator
	stb
)

target_sources(aegis-engine PRIVATE
	"bindless/bindless_buffer.h" 
	"bindless/bindless_buffer.cpp"
	"bindless/bindless_descriptor_set.cpp"
	"bindless/bindless_descriptor_set.h" 
	"bindless/descriptor_handle.h" 
	"bindless/descriptor_handle.cpp"

	"frame_graph/frame_graph.h"
	"frame_graph/frame_graph.cpp"
	"frame_graph/frame_graph_node.h"
	"frame_graph/frame_graph_render_pass.h"
	"frame_graph/frame_graph_resources.h"
	"frame_graph/frame_graph_resources.cpp"
	"frame_graph/frame_graph_resource_pool.h"
	"frame_graph/frame_graph_resource_pool.cpp"

	"render_passes/bloom_pass.cpp"
	"render_passes/bloom_pass.h"
	"render_passes/culling_pass.cpp"
	"render_passes/culling_pass.h"
	"render_passes/geometry_pass.cpp"
	"render_passes/geometry_pass.h"
	"render_passes/gpu_driven_geometry.h"
	"render_passes/gpu_driven_geometry.cpp"
	"render_passes/scene_update_pass.h"
	"render_passes/scene_update_pass.cpp"
	"render_passes/ui_pass.h"
	"render_passes/lighting_pass.cpp"
	"render_passes/lighting_pass.h"
	"render_passes/post_processing_pass.cpp"
	"render_passes/post_processing_pass.h"
	"render_passes/present_pass.h"
	"render_passes/sky_box_pass.cpp"
	"render_passes/sky_box_pass.h"
	"render_passes/ssao_pass.cpp"
	"render_passes/ssao_pass.h"
	"render_passes/transparent_pass.h"
	"render_passes/transparent_pass.cpp" 

	"render_systems/bindless_static_mesh_render_system.h"
	"render_systems/bindless_static_mesh_render_system.cpp"
	"render_systems/point_light_render_system.cpp" 
	"render_systems/point_light_render_system.h" 
	"render_systems/render_system.h"
	"render_systems/static_mesh_render_system.cpp" 
	"render_systems/static_mesh_render_system.h"

	"resources/buffer.cpp"
	"resources/buffer.h"
	"resources/image.cpp"
	"resources/image.h"
	"resources/image_view.cpp"
	"resources/image_view.h"
	"material/material_instance.cpp"
	"material/material_instance.h"
	"material/material_template.cpp"
	"material/material_template.h"
	"resources/mesh_preprocessor.cpp"
	"resources/mesh_preprocessor.h" 
	"resources/sampler.cpp"
	"resources/sampler.h"
	"resources/texture.cpp"
	"resources/texture.h"
	
	"vulkan/debug_utils.h" 
	"vulkan/debug_utils.cpp"
	"vulkan/volk_impl.cpp" 
	"vulkan/volk_include.h" 
	"vulkan/vulkan_context.cpp"
	"vulkan/vulkan_context.h"
	"vulkan/vulkan_tools.cpp"
	"vulkan/vulkan_tools.h"

	"deletion_queue.cpp"
	"deletion_queue.h"
	"descriptors.cpp"
	"descriptors.h"
	"device.cpp"
	"device.h"
	"draw_batch_registry.h"
	"draw_batch_registry.cpp"
	"frame_info.h"
	"frustum.h"
	"globals.h"
	"gpu_timer.h"
	"pipeline.cpp"
	"pipeline.h"
	"renderer.cpp"
	"renderer.h"
	"render_context.h"
	"resources/static_mesh.cpp"
	"resources/static_mesh.h"
	"swap_chain.cpp"
	"swap_chain.h"

)