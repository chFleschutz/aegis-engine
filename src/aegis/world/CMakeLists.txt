add_library(aegis-world STATIC)

target_sources(aegis-world
PUBLIC 
	FILE_SET CXX_MODULES 
	BASE_DIRS "${AEGIS_MODULE_ROOT}"
	FILES
		components.cppm
		component_traits.cppm
		entity.cppm
		registry.cppm
		render_components.cppm
		world.cppm
		system.cppm
		systems/camera_system.cppm
		systems/transform_system.cppm
)

# TODO: remove this once everything is a module
target_include_directories(aegis-world PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/..
)

# PUBLIC: the module interfaces import aegis.math and aegis.core, render_components imports aegis.assets,
# and they export EnTT's names.
target_link_libraries(aegis-world
PUBLIC
	aegis-assets
	aegis-base
	aegis-core
	aegis-math
	EnTT::EnTT
)