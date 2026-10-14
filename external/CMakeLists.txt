set(ENTT_INCLUDE_HEADERS ON)
add_subdirectory(entt)

set(FASTGLTF_COMPILE_AS_CPP20 ON)
add_subdirectory(fastgltf)

set(GLFW_BUILD_DOCS OFF)
add_subdirectory(glfw)

add_subdirectory(glm)

# No prototypes: ImGui's Vulkan backend keeps its own function pointers, which render_graph::UiPass
# fills through the RHI's instance (ImGui_ImplVulkan_LoadFunctions).
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

set(DOCTEST_WITH_TESTS OFF)
set(DOCTEST_NO_INSTALL ON)
add_subdirectory(doctest)
