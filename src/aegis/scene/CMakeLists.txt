add_library(aegis-scene STATIC)

target_sources(aegis-scene
PUBLIC 
	FILE_SET CXX_MODULES 
	BASE_DIRS "${AEGIS_MODULE_ROOT}"
	FILES
		components.cppm
		component_traits.cppm
		description.cppm
		entity.cppm
		scene.cppm
		system.cppm

		loader/fast_gltf_loader.cppm
		loader/gltf_loader.cppm
		loader/loader.cppm
		loader/obj_loader.cppm

		systems/camera_system.cppm
		systems/transform_system.cppm
)

# TODO: remove this once everything is a module
target_include_directories(aegis-scene PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(aegis-scene 
PRIVATE
	aegis-core
	aegis-math
	aegis-scripting
	Aegis::GLTF
	EnTT::EnTT
	tinyobjloader
	fastgltf
)