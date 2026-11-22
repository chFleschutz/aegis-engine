# Build types: CMAKE_BUILD_TYPE (CMAKE_CONFIGURATION_TYPES for a multi-config generator) is one of
#   Debug        no optimisation, asserts and Vulkan validation: for stepping through code
#   Development  Debug with optimisation: NDEBUG stays undefined (asserts and validation on), Debug
#                runtime and debug info; for day-to-day runs
#   Release      optimised, NDEBUG
# Included right after project(): the flags need the compiler, and the MSVC runtime and debug-info
# defaults must be set before any target exists.

set(AEGIS_BUILD_TYPES Debug Development Release)

get_property(AEGIS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(AEGIS_MULTI_CONFIG)
    set(CMAKE_CONFIGURATION_TYPES ${AEGIS_BUILD_TYPES} CACHE STRING "Build types" FORCE)
else()
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    endif()
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS ${AEGIS_BUILD_TYPES})
    if(NOT CMAKE_BUILD_TYPE IN_LIST AEGIS_BUILD_TYPES)
        message(
            FATAL_ERROR
            "CMAKE_BUILD_TYPE is '${CMAKE_BUILD_TYPE}'; use one of: ${AEGIS_BUILD_TYPES}"
        )
    endif()
endif()

# Development compile flags. Debug info comes from CMAKE_MSVC_DEBUG_INFORMATION_FORMAT on Windows
# (both compilers), from -g elsewhere.
foreach(lang C CXX)
    if(CMAKE_${lang}_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        # Not Debug's /Od /Ob0 /RTC1: the runtime checks don't combine with optimisation.
        set(CMAKE_${lang}_FLAGS_DEVELOPMENT "/O2 /Ob1")
    elseif(WIN32)
        set(CMAKE_${lang}_FLAGS_DEVELOPMENT "-O1")
    else()
        set(CMAKE_${lang}_FLAGS_DEVELOPMENT "-O1 -g")
    endif()
endforeach()

# Link like Debug (MSVC: /debug /INCREMENTAL).
foreach(kind EXE SHARED MODULE STATIC)
    set(CMAKE_${kind}_LINKER_FLAGS_DEVELOPMENT "${CMAKE_${kind}_LINKER_FLAGS_DEBUG}")
endforeach()

# Development keeps Debug's runtime (_DEBUG, msvcrtd) and debug info, in the format CMake's default
# picks (a program database with MSVC; Clang's GNU-style driver supports only Embedded). The
# defaults name only Debug and RelWithDebInfo.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug,Development>:Debug>DLL")
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,Development>:ProgramDatabase>")
else()
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,Development>:Embedded>")
endif()

# Imported targets with per-config files: Development links their Debug ones, matching the runtime.
set(CMAKE_MAP_IMPORTED_CONFIG_DEVELOPMENT Debug Release)
