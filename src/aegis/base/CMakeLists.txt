add_library(aegis-base STATIC)
add_library(Aegis::Base ALIAS aegis-base)

# flags.h (AEGIS_FLAGS) is included as <aegis/base/flags.h>.
target_include_directories(aegis-base PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)

target_sources(aegis-base
    PUBLIC
    FILE_SET CXX_MODULES FILES
    base.cppm
    align.cppm
    flags.cppm
    log.cppm
    match.cppm
    profiler.cppm
    PRIVATE
    log.cpp
)
