target_sources(aegis-graphics
PUBLIC 
	FILE_SET CXX_MODULES 
	BASE_DIRS "${AEGIS_MODULE_ROOT}"
	FILES
		bloom_pass.cppm
		culling_pass.cppm
		geometry_pass.cppm
		gpu_driven_geometry.cppm
		lighting_pass.cppm
		post_processing_pass.cppm
		present_pass.cppm
		scene_update_pass.cppm
		sky_box_pass.cppm
		ssao_pass.cppm
		transparent_pass.cppm
		ui_pass.cppm
)
	