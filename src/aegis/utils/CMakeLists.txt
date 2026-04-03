add_library(aegis-utils STATIC)

target_link_libraries(aegis-utils 
PRIVATE
	aegis-math
	Aegis::Log
)

target_sources(aegis-utils
PUBLIC 
	FILE_SET CXX_MODULES 
	BASE_DIRS "${AEGIS_MODULE_ROOT}"
	FILES
		color.cppm
		file.cppm
		rolling_average.cppm
		timer.cppm
		utils.cppm
)

# TODO: remove this once everything is a module
target_include_directories(aegis-utils PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/..
)
