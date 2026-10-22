# Games on the engine's launcher (D7, docs/editor-runtime-split.md).
#
#   aegis_add_game(<Name>
#       SOURCES <a.cpp> ...     # the game's code; one file uses AEGIS_GAME_MODULE (aegis/launch/game_module.h)
#       PROJECT <dir>)          # the game's project folder, its data
#
# Creates
# - <Name>-module: the game's sources, compiled once. An OBJECT library, because a static one would let the
#   linker drop object files nothing references, such as the one with AEGIS_GAME_MODULE.
# - <Name>: the runtime executable, the launcher (src/aegis/launch/main.cpp, the only main()) plus the game.
#   It doesn't link aegis-editor.
# - <Name>-Editor: the same with the editor attached.
# Both look up the project as paths::locate does: --project, then project/ next to the executable, then PROJECT
# (compiled in as AEGIS_GAME_PROJECT_DIR, development builds only). The shaders and the engine content are
# looked up the same way (paths::shaders, paths::content). Nothing is copied: a development build has nothing
# next to its executables and uses the build and source trees, so it never sees stale data; laying out a
# copied or installed game is the export step's job (#134). With tests on, <Name> renders headless as a
# `device` test.
function(aegis_add_game name)
    cmake_parse_arguments(G "" "PROJECT" "SOURCES" ${ARGN})
    if(NOT G_SOURCES OR NOT G_PROJECT)
        message(FATAL_ERROR "aegis_add_game(${name}) needs SOURCES and PROJECT")
    endif()
    cmake_path(ABSOLUTE_PATH G_PROJECT BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
    # Relative to this file, so a project that add_subdirectory()s the engine can call it too.
    cmake_path(SET launcher NORMALIZE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/aegis/launch/main.cpp")

    add_library(${name}-module OBJECT ${G_SOURCES})
    target_link_libraries(${name}-module PUBLIC Aegis::Engine)

    foreach(target IN ITEMS ${name} ${name}-Editor)
        add_executable(${target} "${launcher}")
        target_link_libraries(${target} PRIVATE ${name}-module Aegis::Engine)
        target_compile_definitions(${target} PRIVATE AEGIS_GAME_PROJECT_DIR="${G_PROJECT}")
    endforeach()
    target_link_libraries(${name}-Editor PRIVATE aegis-editor)
    target_compile_definitions(${name}-Editor PRIVATE AEGIS_LAUNCH_EDITOR)

    # The test renders small like the examples' (see examples/CMakeLists.txt). It loads compiled shaders, so it
    # exists only when those are built.
    if(AEGIS_BUILD_TESTS AND TARGET compile_shaders)
        string(TOLOWER ${name} lowered)
        add_test(NAME game-${lowered} COMMAND ${name} --headless --frames 120 --width 640 --height 360)
        set_tests_properties(game-${lowered} PROPERTIES LABELS "device;game" RESOURCE_LOCK gpu TIMEOUT 300)
    endif()
endfunction()
