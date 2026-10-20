add_library(aegis-scripting STATIC)

# PUBLIC: the module interfaces import aegis.math, aegis.platform, aegis.reflect and aegis.scene,
# and use AGX_ASSERT.

target_link_libraries(aegis-scripting 
PUBLIC
	aegis-math
	aegis-core
	aegis-platform
	aegis-reflect
	aegis-scene
)

target_sources(aegis-scripting 
PUBLIC
	FILE_SET CXX_MODULES 
	BASE_DIRS "${AEGIS_MODULE_ROOT}"
	FILES
		script.cppm
		script_system.cppm
		movement/kinematic_movement_controller.cppm
)

target_include_directories(aegis-scripting PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/..
)
