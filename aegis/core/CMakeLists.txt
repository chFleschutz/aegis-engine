add_library(aegis-core STATIC)
add_library(Aegis::Core ALIAS aegis-core)

# flags.h (AEGIS_FLAGS) is included as <aegis/core/flags.h>.
target_include_directories(aegis-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)

target_sources(aegis-core
    PUBLIC
    FILE_SET CXX_MODULES FILES
    core.cppm
    align.cppm
    flags.cppm
    frame_clock.cppm
    log.cppm
    match.cppm
    profiler.cppm
    text.cppm
    PRIVATE
    log.cpp
)
