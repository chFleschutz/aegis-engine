add_library(aegis-paths STATIC)

# The only place the build tree is baked in: paths.cpp is the one TU that reads these. Both derive from
# PROJECT_*_DIR (the engine's project()), not CMAKE_*_DIR, so a parent project that add_subdirectory()s
# the engine gets the same answer from reader and writer. Each can be overridden at run time with the
# environment variable of the same name.
target_compile_definitions(aegis-paths PRIVATE
    AEGIS_SHADER_DIR="${PROJECT_BINARY_DIR}/shaders"
    AEGIS_ASSETS_DIR="${PROJECT_SOURCE_DIR}/modules/aegis-assets"
)

target_sources(aegis-paths
    PUBLIC
    FILE_SET CXX_MODULES FILES
    paths.cppm
    PRIVATE
    paths.cpp
)
