# Test Setup — doctest

Unit-test setup for the new RHI stack (`aegis.rhi` / `renderer` / `platform`, later
`render_graph` / `assets`). Scope is intentionally narrow: pure, GPU-independent logic that
survives the migration. Device-level integration stays in the interactive harness
(`src/aegis/test/`) and the example smoke tests.

## Framework: doctest

Chosen for **minimal compile-time overhead** (single header, ~4× faster to include than Catch2 v3)
in a modules-heavy, warnings-as-errors build. Header-only, trivial single assertions, ships CTest
integration (`doctest_discover_tests`). Vendored as a submodule like every other dependency.

## Layout

Tests live at **root `tests/`** (sibling of `src/`), mirroring **module** names — not source
paths, so the planned flattening of `src/aegis/` never moves them.

**One executable per module** (`aegis-tests-rhi`, `aegis-tests-math`, …), each linking only that
module's library. Editing an RHI test relinks only `aegis-tests-rhi`, not the whole engine's tests.
`ctest` still aggregates cases across every executable, so "run all" stays one command.

```
tests/
  CMakeLists.txt            # helper function + shared doctest main
  test_main.cpp             # DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN only, no tests
  rhi/
    CMakeLists.txt
    free_list.test.cpp
    staging_buffer.test.cpp
    resource_pool.test.cpp
    utility.test.cpp
  math/
    CMakeLists.txt
    math.test.cpp
  render_graph/             # added in Phase 3
    CMakeLists.txt
    frustum.test.cpp
```

Adding a module = new folder + one `aegis_add_test(...)` call; nothing central to edit.

## Naming

| Thing        | Convention                                             | Example                                    |
|--------------|--------------------------------------------------------|--------------------------------------------|
| Executable   | `aegis-tests-<module>`                                 | `aegis-tests-rhi`                          |
| File         | `<unit_under_test>.test.cpp` (named after the class)   | `free_list.test.cpp`                       |
| Subfolder    | mirrors the owning module                              | `tests/rhi/`                               |
| `TEST_SUITE` | namespace-qualified type, one block per file           | `TEST_SUITE("rhi::FreeList")`              |
| `TEST_CASE`  | behavior as a present-tense sentence                   | `"hands out sequential indices"`           |
| `SUBCASE`    | a variation sharing the case's setup                   | `"reuses a freed index"`                   |

```cpp
#include <doctest/doctest.h>

import aegis.rhi;

TEST_SUITE("rhi::FreeList")
{
    TEST_CASE("hands out sequential indices, then reports exhaustion")
    {
        aegis::rhi::FreeList list{ 2 };
        CHECK(list.pop() == 0);
        CHECK(list.pop() == 1);
        CHECK(list.pop() == std::nullopt);
    }
}
```

## CMake integration

**`external/CMakeLists.txt`** (added under the existing `SYSTEM` subdir → headers are system
includes, immune to warnings-as-errors):

```cmake
set(DOCTEST_WITH_TESTS OFF)
set(DOCTEST_NO_INSTALL ON)
add_subdirectory(doctest)
```

**Root `CMakeLists.txt`** (after `add_subdirectory(src)` so targets exist; top-level only):

```cmake
option(AEGIS_BUILD_TESTS "Build unit tests" ${PROJECT_IS_TOP_LEVEL})
if(AEGIS_BUILD_TESTS)
    enable_testing()
    include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
    add_subdirectory(tests)
endif()
```

**`tests/CMakeLists.txt`** — a shared doctest `main` compiled once (a static lib every test exe
links) plus a helper so per-module targets stay boilerplate-free. Module scanning is enabled here
because this is outside the `src/aegis/` scope that sets it:

```cmake
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

# doctest entry point, compiled once and reused by every test executable
add_library(aegis-test-main STATIC test_main.cpp)
target_link_libraries(aegis-test-main PUBLIC doctest::doctest)

function(aegis_add_test module)
    cmake_parse_arguments(T "" "" "SOURCES;LIBS" ${ARGN})
    add_executable(aegis-tests-${module} ${T_SOURCES})
    target_link_libraries(aegis-tests-${module} PRIVATE aegis-test-main ${T_LIBS})
    doctest_discover_tests(aegis-tests-${module})
endfunction()

add_subdirectory(rhi)
add_subdirectory(math)
```

**`tests/rhi/CMakeLists.txt`** — one call, links only what it needs:

```cmake
aegis_add_test(rhi
    SOURCES
        free_list.test.cpp
        staging_buffer.test.cpp
        resource_pool.test.cpp
        utility.test.cpp
    LIBS aegis-rhi
)
```

```cmake
# tests/math/CMakeLists.txt
aegis_add_test(math SOURCES math.test.cpp LIBS aegis-math)
```

**`CMakePresets.json`** — add `testPresets` mirroring the configure presets to keep the flow
preset-driven.

## Running

```
git submodule add https://github.com/doctest/doctest external/doctest

cmake --build --preset windows-clang-debug   # builds all aegis-tests-* targets
ctest --preset windows-clang-debug           # runs every discovered case across them
```

Build or run a single module while iterating:

```
cmake --build --preset windows-clang-debug --target aegis-tests-rhi
ctest --preset windows-clang-debug -R "rhi::"
```

## Recommended practices

- **Test pure logic only for now.** Ring allocators, free lists, generational handles, numeric
  helpers, math. Defer anything needing a live `vk::Device` to the harness/example smoke tests.
- **One executable per module.** Keeps relink cost proportional to what changed and lets each
  binary link only its own library. Split finer only if a module's own test link becomes slow.
- **Compile the doctest `main` once.** The shared `aegis-test-main` static lib avoids recompiling
  the framework impl into every test executable.
- **One assertion focus per `TEST_CASE`;** use `SUBCASE` for variations that share setup rather
  than duplicating cases.
- **Tests are ordinary `.cpp` TUs** that `#include <doctest/doctest.h>` then `import aegis.<mod>;`
  — the framework never touches module interfaces.
- **Name by behavior, not by method.** `TEST_CASE` text should read as an expectation, so a failure
  report states what broke.
- **Prefer `CHECK` over `REQUIRE`** unless a later assertion would crash on the failure — `CHECK`
  keeps going and reports every failure in the case.
- **A new test can double as a bug report.** First `FreeList` test confirms the suspected-inverted
  `pop()` (see the migration plan's testing section).
- **Keep the suite fast and green.** It runs on every build; a slow or flaky unit test defeats the
  purpose. Add device-level coverage later in the Phase 6 headless harness.
- **Mirror new modules as they land.** When `render_graph` / `assets` arrive, add
  `tests/<module>/` with its own `CMakeLists.txt` in the same shape.
