# aegis-bench-math: aegis.math against glm on the plan's six operations (docs/math-library-plan.md, "Testing
# and benchmarks"). Built only with -DAEGIS_BUILD_BENCHMARKS=ON and never part of ctest: timings are not a test.
# Module scanning is enabled here because the tool lives outside the aegis/ scope that sets it.
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

# The gate is the largest accepted ratio of aegis.math's time to glm's; 0 means not gated (-O0 Debug is reported only).
# Release: within 10% of glm. Development: no slower than glm, within the 3% the timings scatter between runs.
add_executable(aegis-bench-math bench_math.cpp)
target_link_libraries(aegis-bench-math PRIVATE aegis-math glm::glm)
target_compile_definitions(aegis-bench-math PRIVATE
    "AEGIS_BENCH_GATE=$<IF:$<CONFIG:Release>,1.10,$<IF:$<CONFIG:Development>,1.03,0.0>>")
