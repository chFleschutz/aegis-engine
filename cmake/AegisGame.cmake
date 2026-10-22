# Games on the engine's launcher (D7, docs/editor-runtime-split.md).
#
#   aegis_add_game(<Name>
#       SOURCES <a.cpp> ...     # the game's code; one file uses AEGIS_GAME_MODULE (aegis/launch/game_module.h)
#       PROJECT <dir>           # the game's project folder, its data
#       [SCENES <path> ...])    # scene files of the project to run as device tests (--scene)
#
# Creates
# - <Name>-module: the game's sources, compiled once. An OBJECT library, because a static one would let the
#   linker drop object files nothing references, such as the one with AEGIS_GAME_MODULE.
# - <Name>: the runtime executable, the launcher (src/aegis/launch/main.cpp, the only main()) plus the game.
#   It doesn't link aegis-editor.
# - <Name>-Editor: the same with the editor's shell (editor::EditorShell) installed.
# Both look up the project as paths::locate does: --project, then project/ next to the executable, then PROJECT
# (compiled in as AEGIS_GAME_PROJECT_DIR, development builds only). The shaders and the engine content are
# looked up the same way (paths::shaders, paths::content). Nothing is copied: a development build has nothing
# next to its executables and uses the build and source trees, so it never sees stale data; laying out a
# copied or installed game is the export step's job (#134). With tests on, <Name> renders headless as a
# `device` test: once per scene of SCENES (game-<name>-<scene file stem>), else once with its startup scene.
function(aegis_add_game name)
    cmake_parse_arguments(G "" "PROJECT" "SOURCES;SCENES" ${ARGN})
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

    # Each test renders headless for a fixed number of frames; the exit code fails on a startup error, a render
    # error or a validation error. The `gpu` lock keeps device runs from overlapping. They render at 640x360,
    # not the 1920x1080 default: a smoke test gains nothing from the pixels, and on lavapipe (CI) the run time
    # grows with them. They load compiled shaders, so they exist only when those are built.
    if(AEGIS_BUILD_TESTS AND TARGET compile_shaders)
        string(TOLOWER ${name} lowered)
        set(args --headless --frames 120 --width 640 --height 360)
        if(G_SCENES)
            foreach(scene IN LISTS G_SCENES)
                cmake_path(GET scene STEM stem)
                string(TOLOWER "game-${lowered}-${stem}" test)
                add_test(NAME ${test} COMMAND ${name} ${args} --scene ${scene})
                set_tests_properties(${test} PROPERTIES LABELS "device;game" RESOURCE_LOCK gpu TIMEOUT 300)
            endforeach()
        else()
            add_test(NAME game-${lowered} COMMAND ${name} ${args})
            set_tests_properties(game-${lowered} PROPERTIES LABELS "device;game" RESOURCE_LOCK gpu TIMEOUT 300)
        endif()
    endif()
endfunction()
