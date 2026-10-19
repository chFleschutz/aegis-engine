add_library(aegis-math STATIC)

target_link_libraries(aegis-math 
PUBLIC
	aegis-assert
PRIVATE
	glm::glm
	Aegis::Log
)

target_sources(aegis-math 
PUBLIC 
	FILE_SET CXX_MODULES 
	BASE_DIRS "${AEGIS_MODULE_ROOT}"
	FILES
		math.cppm
)

# TODO: remove this once everything is a module
target_include_directories(aegis-math PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/..
)
