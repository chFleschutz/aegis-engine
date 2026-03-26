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
		loader/obj_loader.cppm

		systems/camera_system.cppm
		systems/transform_system.cppm
)

# TODO: remove this once everything is a module
target_include_directories(aegis-scene PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/..
)

# TODO: remove this once everything is a module
target_link_libraries(aegis-scene 
PRIVATE
	aegis-math
	Aegis::GLTF
	EnTT::EnTT
	tinyobjloader
)

target_sources(aegis-engine PRIVATE
	"loader/fast_gltf_loader.h"
	"loader/fast_gltf_loader.cpp"
	"loader/gltf_loader.cpp"
	"loader/gltf_loader.h"
	"loader/obj_loader.cpp"
	"loader/obj_loader.h"

	"systems/camera_system.cpp"
	"systems/camera_system.h"
	"systems/transform_system.cpp"
	"systems/transform_system.h"
	
	"components.h"
	"description.h"
	"entity.cpp"
	"entity.h"
	"scene.cpp"
	"scene.h"
	"system.h"

)