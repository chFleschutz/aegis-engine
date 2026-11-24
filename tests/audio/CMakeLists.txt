aegis_add_test(
    audio
    SOURCES audio_engine.test.cpp audio_system.test.cpp decoder.test.cpp
    LIBS aegis-audio
)

# decoder.test.cpp decodes the tones in data/ (made for these tests: a 440 Hz sine, mono WAV and stereo Ogg Vorbis).
target_compile_definitions(
    aegis-tests-audio
    PRIVATE AEGIS_TEST_AUDIO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)
