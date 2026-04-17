target_sources(aegis-graphics 
PUBLIC 
	FILE_SET CXX_MODULES 
	BASE_DIRS "${AEGIS_MODULE_ROOT}"
	FILES
		bindless.cppm
		bindless_buffer.cppm
		bindless_descriptor_set.cppm
		descriptor_handle.cppm
)
