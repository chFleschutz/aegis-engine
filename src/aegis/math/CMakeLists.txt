add_library(aegis-math STATIC)

target_link_libraries(aegis-math 
PRIVATE
	glm::glm
)

target_sources(aegis-math 
PUBLIC 
	FILE_SET CXX_MODULES 
	BASE_DIRS "${AEGIS_MODULE_ROOT}"
	FILES
		interpolation.cppm
		math.cppm
		perlin_noise.cppm
		random.cppm
)

target_sources(aegis-engine PRIVATE
	interpolation.cpp
	interpolation.h
	math.cpp
	math.h
	perlin_noise.cpp
	perlin_noise.h
	print.h
	random.cpp
	random.h
)

# TODO: remove this once everything is a module
target_include_directories(aegis-math PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/..
)

# TODO: remove this once everything is a module
target_link_libraries(aegis-math PRIVATE
	Aegis::Log
)