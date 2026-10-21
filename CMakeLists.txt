cmake_minimum_required(VERSION 4.5)

project(AegisEngine)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# `import std` (CMake 4.5+) builds the standard library module with the flags of each target that
# imports it, warnings-as-errors included. Clang warns on the std module sources themselves (they
# name a module `std` and include headers in its purview), so silence just those two.
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Wno-reserved-module-identifier;-Wno-include-angled-in-module-purview>")
endif()

option(BUILD_EXAMPLES "Build example projects" ON)
option(COMPILE_SHADERS "Compile shaders to SPIR-V" ON)

# Find the Vulkan package
find_package(Vulkan REQUIRED)
if(NOT Vulkan_FOUND)
    message(FATAL_ERROR "Vulkan was not found. Make sure the Vulkan SDK is installed")
endif()

# Slang shader compiler, from the same Vulkan SDK as the headers (Vulkan_INCLUDE_DIR is <sdk>/Include,
# or <sdk>/x86_64/include on Linux). Found here, before any subdirectory: both shaders/ and aegis-test
# (src/aegis/test) compile with it.
cmake_path(GET Vulkan_INCLUDE_DIR PARENT_PATH AEGIS_VULKAN_SDK_DIR)
find_program(SLANGC_EXE slangc
    HINTS "${AEGIS_VULKAN_SDK_DIR}/Bin" "${AEGIS_VULKAN_SDK_DIR}/bin" "$ENV{VULKAN_SDK}/bin" "$ENV{VULKAN_SDK}/Bin"
    REQUIRED
)
include(cmake/AegisShaders.cmake)
include(cmake/AegisGame.cmake)

add_subdirectory(external SYSTEM)
add_subdirectory(src)

# Before tests/: the device tests register only when the shaders they load are built.
if(COMPILE_SHADERS)
    add_subdirectory(shaders)
endif()

option(AEGIS_BUILD_TESTS "Build unit tests" ${PROJECT_IS_TOP_LEVEL})
if(AEGIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Print configuration
message("")
message("CMake Configuration:")
message(STATUS "CMake Version: ${CMAKE_VERSION}")
message(STATUS "CMake Generator: ${CMAKE_GENERATOR}")
message(STATUS "CMake Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "CMake source directory: ${CMAKE_SOURCE_DIR}")
message(STATUS "CMake binary directory: ${CMAKE_BINARY_DIR}")
message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "Vulkan SDK ${Vulkan_VERSION}: ${Vulkan_LIBRARY}")
message(STATUS "Slang compiler: ${SLANGC_EXE}")
message("")
