target_sources(aegis-graphics
PUBLIC 
	FILE_SET CXX_MODULES 
	BASE_DIRS "${AEGIS_MODULE_ROOT}"
	FILES
		bindless_static_mesh_render_system.cppm
		point_light_render_system.cppm
		render_system.cppm
		static_mesh_render_system.cppm
)
	