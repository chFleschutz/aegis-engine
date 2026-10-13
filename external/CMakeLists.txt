set(ENTT_INCLUDE_HEADERS ON)
add_subdirectory(entt)

set(FASTGLTF_COMPILE_AS_CPP20 ON)
add_subdirectory(fastgltf)

set(GLFW_BUILD_DOCS OFF)
add_subdirectory(glfw)

add_subdirectory(glm)

# No volk and no prototypes: ImGui's Vulkan backend keeps its own function pointers, filled by
# ImGui_ImplVulkan_LoadFunctions from whichever stack initializes it. Prototypes would clash with
# volk's same-named globals (the old stack's binaries resolved them to volk's data).
set(IMGUI_USE_VOLK OFF)
add_subdirectory(imgui)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES)

add_subdirectory(imguizmo)

add_subdirectory(meshoptimizer)

# stb is header-only; its implementation is compiled once here, so the old and new texture code can
# both decode images without duplicate symbols.
add_library(stb STATIC stb_image_impl.cpp)
target_include_directories(stb SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tinyobjloader)

add_subdirectory(vma)

add_subdirectory(volk)

set(DOCTEST_WITH_TESTS OFF)
set(DOCTEST_NO_INSTALL ON)
add_subdirectory(doctest)
