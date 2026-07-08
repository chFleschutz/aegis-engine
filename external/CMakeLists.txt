set(ENTT_INCLUDE_HEADERS ON)
add_subdirectory(entt)

set(FASTGLTF_COMPILE_AS_CPP20 ON)
add_subdirectory(fastgltf)

set(GLFW_BUILD_DOCS OFF)
add_subdirectory(glfw)

add_subdirectory(glm)

set(IMGUI_USE_VOLK ON)
add_subdirectory(imgui)

add_subdirectory(imguizmo)

add_subdirectory(meshoptimizer)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tinyobjloader)

add_subdirectory(vma)

add_subdirectory(volk)

set(DOCTEST_WITH_TESTS OFF)
set(DOCTEST_NO_INSTALL ON)
add_subdirectory(doctest)
