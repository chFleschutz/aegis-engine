add_library(aegis-ui STATIC)

target_link_libraries(aegis-ui
PUBLIC
	aegis-core
	aegis-graphics
	imgui::imgui
	imguizmo
)

target_sources(aegis-ui
PUBLIC
	FILE_SET CXX_MODULES
	BASE_DIRS "${AEGIS_MODULE_ROOT}"
	FILES
		ui.cppm
)
