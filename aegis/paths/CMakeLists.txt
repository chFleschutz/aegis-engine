add_library(aegis-paths STATIC)

# The only place the build tree is baked in: paths.cpp is the one TU that reads these. Both derive from
# PROJECT_*_DIR (the engine's project()), not CMAKE_*_DIR, so a parent project that add_subdirectory()s
# the engine gets the same answer from reader and writer. Each can be overridden at run time with the
# environment variable of the same name.
target_compile_definitions(aegis-paths PRIVATE
    AEGIS_SHADER_DIR="${PROJECT_BINARY_DIR}/shaders"
    AEGIS_PROJECT_DIR="${PROJECT_SOURCE_DIR}/samples"
    AEGIS_CONTENT_DIR="${PROJECT_SOURCE_DIR}/content"
    AEGIS_CACHE_DIR="${PROJECT_BINARY_DIR}/.aegis"
)

target_sources(aegis-paths
    PUBLIC
    FILE_SET CXX_MODULES FILES
    paths.cppm
    PRIVATE
    paths.cpp
)
