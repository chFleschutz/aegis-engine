# AGX_ASSERT's failure handler (core/assert.h). A library of its own because aegis-math, below
# aegis-core, asserts too.
add_library(aegis-assert STATIC assert.cpp)
target_include_directories(aegis-assert PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_library(aegis-core STATIC)

target_link_libraries(aegis-core 
PUBLIC
	aegis-assert
	aegis-base
)

target_sources(aegis-core
PUBLIC 
	FILE_SET CXX_MODULES 
	BASE_DIRS "${AEGIS_MODULE_ROOT}"
	FILES
		asset.cppm
		globals.cppm
		layer.cppm
		layer_stack.cppm
)

# TODO: remove this once everything is a module
target_include_directories(aegis-core PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/..
)
