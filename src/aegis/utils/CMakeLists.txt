add_library(aegis-utils STATIC)

target_link_libraries(aegis-utils 
PRIVATE
	aegis-math
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

target_sources(aegis-engine 
PRIVATE
	"color.cpp"
	"color.h"
	"factory.h"
	"file.cpp"
	"file.h"
	"rolling_average.h"
	"timer.h"
	"utils.h"
)

# TODO: remove this once everything is a module
target_include_directories(aegis-utils PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/..
)

# TODO: remove this once everything is a module
target_link_libraries(aegis-utils PRIVATE
	Aegis::Log
)