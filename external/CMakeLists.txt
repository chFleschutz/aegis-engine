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

# libktx's KTX2 reader and writer: the part of KTX-Software in ktx/ (tools/update-ktx.sh), built here
# instead of through its CMakeLists.txt, which needs the whole repository. ktx_stubs.c stands in for the zlib and
# KTX1 code it leaves out; version.h is what upstream's build generates. Its zstd supercompression is zstd's own.
file(STRINGS ktx/IMPORTED_VERSION KTX_IMPORTED_VERSION LIMIT_COUNT 1)
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ktx-generated/version.h
    CONTENT "#define LIBKTX_VERSION ${KTX_IMPORTED_VERSION}\n#define LIBKTX_DEFAULT_VERSION v4.0.__default__\n")
add_library(ktx STATIC
    ktx_stubs.c
    ktx/lib/checkheader.c
    ktx/lib/filestream.c
    ktx/lib/hashlist.c
    ktx/lib/memstream.c
    ktx/lib/strings.c
    ktx/lib/swap.c
    ktx/lib/texture.c
    ktx/lib/texture2.c
    ktx/lib/vkformat_check.c
    ktx/lib/vkformat_check_variant.c
    ktx/lib/vkformat_typesize.c
    ktx/lib/writer2.c
    ktx/external/dfdutils/colourspaces.c
    ktx/external/dfdutils/createdfd.c
    ktx/external/dfdutils/interpretdfd.c
    ktx/external/dfdutils/queries.c
    ktx/external/dfdutils/vk2dfd.c
)
target_include_directories(ktx SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/ktx/include)
target_include_directories(ktx PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/ktx-generated
    ktx/lib
    ktx/other_include
    ktx/external
    ktx/utils
)
target_compile_definitions(ktx
    PUBLIC KHRONOS_STATIC
    PRIVATE LIBKTX KTX_FEATURE_KTX2 KTX_FEATURE_WRITE KTX_OMIT_VULKAN=1
)
target_link_libraries(ktx PRIVATE libzstd_static)
# Upstream code: no warnings of ours, so a new compiler's warning does not fail the build.
set_target_properties(ktx PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
target_compile_options(ktx PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/w,-w>)

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

# zstd: libktx's supercompression. The static library only; its CMakeLists.txt is in build/cmake.
set(ZSTD_BUILD_STATIC ON)
set(ZSTD_BUILD_SHARED OFF)
set(ZSTD_BUILD_PROGRAMS OFF)
set(ZSTD_BUILD_TESTS OFF)
set(ZSTD_LEGACY_SUPPORT OFF)
set(ZSTD_MULTITHREAD_SUPPORT OFF)
add_subdirectory(zstd/build/cmake EXCLUDE_FROM_ALL)
# Upstream code, like libktx: no warnings of ours.
set_target_properties(libzstd_static PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
target_compile_options(libzstd_static PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/w,-w>)
