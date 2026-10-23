add_library(aegis-world STATIC)

target_sources(aegis-world
PUBLIC 
	FILE_SET CXX_MODULES 
	BASE_DIRS "${AEGIS_MODULE_ROOT}"
	FILES
		components.cppm
		entity.cppm
		registry.cppm
		render_components.cppm
		types.cppm
		world.cppm
		system.cppm
		systems/camera_system.cppm
		systems/transform_system.cppm
PRIVATE
	registry.cpp
	types.cpp
	world.cpp
	systems/camera_system.cpp
	systems/transform_system.cpp
)

# TODO: remove this once everything is a module
target_include_directories(aegis-world PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/..
)

# PUBLIC: the module interfaces import aegis.math, render_components imports aegis.assets,
# types imports aegis.reflect, and they export EnTT's names.
target_link_libraries(aegis-world
PUBLIC
	aegis-assets
	aegis-core
	aegis-math
	aegis-reflect
	EnTT::EnTT
)