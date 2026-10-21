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
# tests/test_main.cpp compiles doctest's main once; its prebuilt doctest_with_main (~30 TUs) goes unused.
set(DOCTEST_WITH_MAIN_IN_STATIC_LIB OFF)
add_subdirectory(doctest)

# glaze is header-only; its templates are heavy, so only .cpp files include it.
set(glaze_DEVELOPER_MODE OFF)
set(glaze_BUILD_EXAMPLES OFF)
add_subdirectory(glaze)

# xxHash: XXH3_64 for entity ID derivation and cache keys. Its implementation is compiled once here;
# include xxhash.h only in .cpp files.
add_library(xxhash STATIC xxhash_impl.c)
target_include_directories(xxhash SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/xxhash)
