add_library(aegis-physics STATIC)

target_link_libraries(aegis-physics
PRIVATE
	aegis-math
	aegis-utils
	Aegis::Log
)

target_sources(aegis-physics 
PUBLIC 
	FILE_SET CXX_MODULES 
	BASE_DIRS "${AEGIS_MODULE_ROOT}"
	FILES
		motion_dynamics.cppm
)

# TODO: remove this once everything is a module
target_include_directories(aegis-physics PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_sources(aegis-engine PRIVATE
	"motion_dynamics.cpp"
	"motion_dynamics.h"
)