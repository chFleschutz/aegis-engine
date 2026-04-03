add_library(aegis-ui STATIC)

target_link_libraries(aegis-ui
PRIVATE
	aegis-core
	aegis-graphics
	imgui::imgui
)

target_sources(aegis-ui
PUBLIC
	FILE_SET CXX_MODULES
	BASE_DIRS "${AEGIS_MODULE_ROOT}"
	FILES
		ui.cppm
		panels/demo_panel.cppm
		panels/menu_bar_panel.cppm
		panels/profiler_panel.cppm
		panels/renderer_panel.cppm
		panels/scene_panel.cppm
		panels/statistics_panel.cppm
)
