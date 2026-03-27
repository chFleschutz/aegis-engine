add_library(aegis-scripting STATIC)

target_link_libraries(aegis-scripting 
PRIVATE
	aegis-core
	aegis-scene
)

target_sources(aegis-scripting 
PUBLIC
	FILE_SET CXX_MODULES 
	BASE_DIRS "${AEGIS_MODULE_ROOT}"
	FILES
		script_base.cppm
		script_manager.cppm
		movement/dynamic_movement_controller.cppm
		movement/kinematic_movement_controller.cppm
		movement/world_border.cppm
)

target_sources(aegis-engine PRIVATE
	"movement/dynamic_movement_controller.cpp"
	"movement/dynamic_movement_controller.h"
	"movement/kinematic_movement_controller.cpp"
	"movement/kinematic_movement_controller.h"
	"movement/world_border.h"
	"script_base.h"
	"script_manager.cpp"
	"script_manager.h"
	"test_script.h"
)