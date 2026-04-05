target_sources(aegis-graphics
PUBLIC 
	FILE_SET CXX_MODULES 
	BASE_DIRS "${AEGIS_MODULE_ROOT}"
	FILES
		fast_gltf_loader.cppm
		gltf_loader.cppm
		loader.cppm
		obj_loader.cppm
)
