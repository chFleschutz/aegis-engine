add_library(aegis-scripting STATIC)

target_link_libraries(aegis-scripting 
PRIVATE
	aegis-core
	aegis-math
	aegis-scene
	aegis-utils
	Aegis::Log
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
		physics/motion_dynamics.cppm
)

target_include_directories(aegis-scripting PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/..
)
