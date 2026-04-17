target_sources(aegis-graphics 
PUBLIC 
	FILE_SET CXX_MODULES 
	BASE_DIRS "${AEGIS_MODULE_ROOT}"
	FILES
		frame_graph.cppm
		frame_graph_node.cppm
		frame_graph_render_pass.cppm
		frame_graph_resources.cppm
		frame_graph_resource_handle.cppm
		frame_graph_resource_pool.cppm
)
