cmake_minimum_required(VERSION 3.28)

project(AegisEngine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

option(BUILD_EXAMPLES "Build example projects" ON)
option(COMPILE_SHADERS "Compile GLSL shaders to SPIR-V" ON)

# Find the Vulkan package
find_package(Vulkan REQUIRED)
if(NOT Vulkan_FOUND)
    message(FATAL_ERROR "Vulkan was not found. Make sure the Vulkan SDK is installed")
endif()

add_subdirectory(external)
add_subdirectory(modules)
add_subdirectory(src)

set(PROJECT_DIR "${CMAKE_SOURCE_DIR}")
set(BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}")

target_compile_definitions(aegis-engine PUBLIC
    PROJECT_DIR="${CMAKE_SOURCE_DIR}"
    BUILD_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(COMPILE_SHADERS)
    add_subdirectory(shaders)
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
message("")
