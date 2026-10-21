add_subdirectory(eval-scene)
add_subdirectory(helmets)
add_subdirectory(simple-scene)
add_subdirectory(sponza)
add_subdirectory(template)

# Every example renders headless for a fixed number of frames as a `device` test (`ctest -LE device` skips
# them); the exit code fails on a startup error, a render error or a validation error. The `gpu` lock keeps
# device runs from overlapping. They load compiled shaders, so they exist only when those are built.
if(AEGIS_BUILD_TESTS AND TARGET compile_shaders)
    foreach(example IN ITEMS Eval-Scene Helmets Simple-Scene Sponza Template-Scene)
        string(TOLOWER ${example} lowered)
        add_test(NAME example-${lowered} COMMAND ${example} --headless --frames 120)
        set_tests_properties(example-${lowered} PROPERTIES LABELS "device;example" RESOURCE_LOCK gpu TIMEOUT 300)
    endforeach()
endif()
