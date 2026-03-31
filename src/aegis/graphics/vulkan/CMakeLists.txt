target_sources(aegis-graphics 
PUBLIC 
	FILE_SET CXX_MODULES 
	BASE_DIRS "${AEGIS_MODULE_ROOT}"
	FILES
		debug_utils.cppm
		
		vulkan_context.cppm
		vulkan_tools.cppm
PRIVATE
	volk_impl.cpp
)
