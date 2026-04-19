add_library(aegis-rhi STATIC)

target_sources(aegis-rhi
PUBLIC 
	FILE_SET CXX_MODULES 
	FILES
		context.cppm
		device.cppm
		rhi.cppm
)

add_subdirectory(vulkan)
