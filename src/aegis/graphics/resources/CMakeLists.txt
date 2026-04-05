target_sources(aegis-graphics 
PUBLIC 
	FILE_SET CXX_MODULES 
	BASE_DIRS "${AEGIS_MODULE_ROOT}"
	FILES
		buffer.cppm
		image.cppm
		image_view.cppm
		mesh_preprocessor.cppm
		sampler.cppm
		static_mesh.cppm
		texture.cppm
		vertex.cppm
)
