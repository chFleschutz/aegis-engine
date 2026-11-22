add_library(aegis-math STATIC)

target_sources(
    aegis-math
    PUBLIC
        FILE_SET CXX_MODULES
            BASE_DIRS "${AEGIS_MODULE_ROOT}"
            FILES
                math.cppm
                aliases.cppm
                matrix.cppm
                quaternion.cppm
                scalar.cppm
                transform.cppm
                vector.cppm
)
