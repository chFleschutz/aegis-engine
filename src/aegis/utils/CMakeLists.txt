target_sources(aegis-engine 
PUBLIC 
	FILE_SET CXX_MODULES 
	BASE_DIRS "${AEGIS_MODULE_ROOT}"
	FILES
		color.cppm
		file.cppm
		rolling_average.cppm
		timer.cppm
		utils.cppm
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