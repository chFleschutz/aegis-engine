add_library(aegis-audio STATIC)

target_sources(
    aegis-audio
    PUBLIC
        FILE_SET CXX_MODULES
            BASE_DIRS "${AEGIS_MODULE_ROOT}"
            FILES
                audio.cppm
                audio_engine.cppm
                audio_source.cppm
                audio_system.cppm
                decoder.cppm
                error.cppm
    PRIVATE audio_engine.cpp audio_source.cpp audio_system.cpp decoder.cpp
)

# PUBLIC: the module interfaces import aegis.assets (AudioClip), aegis.math, aegis.reflect and aegis.world.
# PRIVATE: miniaudio's header is included only in the .cpp files.
target_link_libraries(
    aegis-audio
    PUBLIC aegis-assets aegis-math aegis-reflect aegis-world
    PRIVATE aegis-core miniaudio
)
