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
# copied next to the executables, so a copied output folder finds them. With tests on, <Name> renders headless
# as a `device` test.
function(aegis_add_game name)
    cmake_parse_arguments(G "" "PROJECT" "SOURCES" ${ARGN})
    if(NOT G_SOURCES OR NOT G_PROJECT)
        message(FATAL_ERROR "aegis_add_game(${name}) needs SOURCES and PROJECT")
    endif()
    cmake_path(ABSOLUTE_PATH G_PROJECT BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
    # Relative to this file, so a project that add_subdirectory()s the engine can call it too.
    cmake_path(SET launcher NORMALIZE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/aegis/launch/main.cpp")
    cmake_path(SET content NORMALIZE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../content")

    add_library(${name}-module OBJECT ${G_SOURCES})
    target_link_libraries(${name}-module PUBLIC Aegis::Engine)

    foreach(target IN ITEMS ${name} ${name}-Editor)
        add_executable(${target} "${launcher}")
        target_link_libraries(${target} PRIVATE ${name}-module Aegis::Engine)
        target_compile_definitions(${target} PRIVATE AEGIS_GAME_PROJECT_DIR="${G_PROJECT}")
    endforeach()
    target_link_libraries(${name}-Editor PRIVATE aegis-editor)
    target_compile_definitions(${name}-Editor PRIVATE AEGIS_LAUNCH_EDITOR)

    # Both executables share one output folder, so one copy serves both.
    if(TARGET compile_shaders)
        add_custom_target(${name}-data ALL
            COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                "$<TARGET_PROPERTY:compile_shaders,BINARY_DIR>" "$<TARGET_FILE_DIR:${name}>/shaders"
            COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                "${content}" "$<TARGET_FILE_DIR:${name}>/content"
            COMMENT "Copying the shaders and the engine content next to ${name}"
            VERBATIM)
        add_dependencies(${name}-data compile_shaders)
        add_dependencies(${name} ${name}-data)
        add_dependencies(${name}-Editor ${name}-data)

        if(AEGIS_BUILD_TESTS)
            string(TOLOWER ${name} lowered)
            add_test(NAME game-${lowered} COMMAND ${name} --headless --frames 120)
            set_tests_properties(game-${lowered} PROPERTIES LABELS "device;game" RESOURCE_LOCK gpu TIMEOUT 300)
        endif()
    endif()
endfunction()
