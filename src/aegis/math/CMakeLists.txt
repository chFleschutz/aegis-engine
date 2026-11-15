add_library(aegis-math STATIC)

target_link_libraries(aegis-math 
PRIVATE
	glm::glm
)

target_sources(aegis-math 
PUBLIC 
	FILE_SET CXX_MODULES 
	BASE_DIRS "${AEGIS_MODULE_ROOT}"
	FILES
		math.cppm
)
