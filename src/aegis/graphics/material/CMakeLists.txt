target_sources(aegis-graphics 
PUBLIC 
	FILE_SET CXX_MODULES 
	BASE_DIRS "${AEGIS_MODULE_ROOT}"
	FILES
		material_instance.cppm
		material_template.cppm
)
