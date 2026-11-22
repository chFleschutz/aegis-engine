add_library(aegis-engine STATIC)
add_library(Aegis::Engine ALIAS aegis-engine)

target_include_directories(aegis-engine PUBLIC ${AEGIS_MODULE_ROOT}/.. ${AEGIS_MODULE_ROOT})

target_include_directories(aegis-engine PRIVATE ${AEGIS_MODULE_ROOT}/..)

target_link_libraries(
    aegis-engine
    PUBLIC
        aegis-asset-library
        aegis-assets
        aegis-core
        aegis-math # aegis.game re-exports it
        aegis-paths # The launcher (launch/main.cpp, compiled into each game) locates the project
        aegis-platform
        aegis-reflect
        aegis-render-bridge
        aegis-render-graph
        aegis-render-passes # GameModule::buildGraph and Shell::buildGraph declare passes
        aegis-renderer
        aegis-rhi
        aegis-world
        aegis-scripting
        aegis-serialize
)

target_link_libraries(
    aegis-engine
    PRIVATE
        imgui::imgui # Presentation: the ImGui context and its GLFW backend
        stb # stb_image_write for the headless screenshot
)

target_sources(
    aegis-engine
    PRIVATE engine.cpp presentation.cpp project_file.cpp scene_defaults.cpp scene_description.cpp
    PUBLIC
        FILE_SET CXX_MODULES
            FILES engine.cppm game.cppm project_file.cppm scene_defaults.cppm scene_description.cppm
)

if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    target_link_libraries(aegis-engine PRIVATE ${X11_LIBRARIES})
endif()
