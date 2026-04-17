add_library(aegis-editor STATIC)

target_link_libraries(aegis-editor
PUBLIC
	aegis-core
	aegis-graphics
	aegis-scene
	imgui
	imguizmo
)

target_include_directories(aegis-editor PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_sources(aegis-editor
PUBLIC
	FILE_SET CXX_MODULES
	BASE_DIRS "${AEGIS_MODULE_ROOT}"
	FILES
		editor_layer.cppm
		panels/demo_panel.cppm
		panels/menu_bar_panel.cppm
		panels/panels.cppm
		panels/profiler_panel.cppm
		panels/renderer_panel.cppm
		panels/scene_panel.cppm
		panels/statistics_panel.cppm
)
