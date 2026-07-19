# Tests

Unit tests for the new RHI stack (`aegis.rhi` / `renderer` / `platform`, later `render_graph` /
`assets`). Scope is intentionally narrow: **pure, GPU-independent logic** that survives the
migration. Device-level integration stays in the interactive harness (`../src/aegis/test/`) and the
example smoke tests.

## Quick start

```
cmake --build --preset windows-clang-debug   # builds all aegis-tests-* targets
ctest --preset windows-clang-debug           # runs every discovered case
```

Iterating on one module:

```
cmake --build --preset windows-clang-debug --target aegis-tests-rhi
ctest --preset windows-clang-debug -R "rhi::"
```

Currently 36 cases across `aegis-tests-rhi` and `aegis-tests-math`.

### Device-level check

Unit tests are not sufficient when a change touches live upload or swapchain code:

```
cmake --build --preset windows-clang-debug --target aegis-test
build\windows-clang-debug\src\aegis\test\aegis-test.exe
```

`../src/aegis/test/main.cpp` is the only `main()` that reaches a real `rhi::Device` — the
`examples/` targets still run on the old `Aegis::Graphics` stack and will not catch an RHI
regression. It should open its window and draw the triangle. Validation layers are on in debug
builds, so a silent stderr is part of the signal.

## What we test

Pure logic that needs no live `rhi::Device`: ring/offset math, free lists, generational handles,
`alignTo`, swapchain sizing policy, mip-chain arithmetic, and `Aegis::Math`.

Anything whose only constructor touches the GPU belongs in the device-level harness — or gets its
pure part extracted (see below).

Invariants guarded by `assert` (e.g. `ResourcePool` stale-handle rejection) abort rather than
return, so test the behavior *around* them, not the abort.

## Layout and naming

Tests live at root `tests/`, mirroring **module** names — not source paths, so the planned
flattening of `src/aegis/` never moves them.

```
tests/
  CMakeLists.txt       # aegis_add_test helper + shared doctest main
  test_main.cpp        # DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN only, no tests
  rhi/
    CMakeLists.txt
    *.test.cpp
  math/
    CMakeLists.txt
    *.test.cpp
```

| Thing        | Convention                                            | Example                          |
|--------------|-------------------------------------------------------|----------------------------------|
| Executable   | `aegis-tests-<module>`                                | `aegis-tests-rhi`                |
| File         | `<unit_under_test>.test.cpp`, named after the class   | `free_list.test.cpp`             |
| Subfolder    | mirrors the owning module                             | `tests/rhi/`                     |
| `TEST_SUITE` | namespace-qualified type, one block per file          | `TEST_SUITE("rhi::FreeList")`    |
| `TEST_CASE`  | behavior as a present-tense sentence                  | `"hands out sequential indices"` |
| `SUBCASE`    | a variation sharing the case's setup                  | `"reuses a freed index"`         |

Tests are ordinary `.cpp` TUs — the framework never touches module interfaces:

```cpp
#include <doctest/doctest.h>

import aegis.rhi;

TEST_SUITE("rhi::FreeList")
{
    TEST_CASE("hands out sequential indices, then reports exhaustion")
    {
        aegis::rhi::FreeList list{ 2 };
        CHECK(list.pop() == std::optional<std::uint32_t>{ 0 });
        CHECK(list.pop() == std::optional<std::uint32_t>{ 1 });
        CHECK(list.pop() == std::nullopt);
    }
}
```

## Adding tests

**To an existing module:** add `<unit>.test.cpp` and list it in that module's `CMakeLists.txt`.

**A new module:** create `tests/<module>/CMakeLists.txt` with one call, then `add_subdirectory` it
from `CMakeLists.txt` here.

```cmake
aegis_add_test(<module> SOURCES <unit>.test.cpp LIBS aegis-<module>)
```

`aegis_add_test` creates `aegis-tests-<module>`, links `aegis-test-main` plus `LIBS`, and registers
each case with CTest via `doctest_discover_tests`. Nothing central needs editing.

> **Re-export rule (gotcha):** a test TU only sees what the *primary* module interface re-exports.
> A type must be `export import`-ed from the umbrella (e.g. `rhi.cppm`) — partition-level `export`
> alone is invisible to tests.

## Growing the testable surface: extract, don't mock

The limit on RHI coverage is not the absence of a device harness — it is pure logic *trapped*
behind Vulkan-owning types. An early draft of this doc listed a `staging_buffer.test.cpp` that was
never written, because `StagingBuffer` holds a `Device*` and its destructor calls `m_device->free()`,
even though `allocate`/`isRegionFree` were pure ring arithmetic. The fix was to extract, not to
stand up a device:

- **`RingAllocator`** (`:ring_allocator`) — the ring math lifted out of `StagingBuffer` verbatim.
  `StagingBuffer` now holds one and only adds the base pointer. This is where the module's
  wrap-around and `AllocError` coverage lives.
- **`chooseSwapchainExtent` / `chooseImageCount`** (`aegis::rhi::detail`, in `:swapchain`) —
  swapchain sizing policy as free functions over plain values. `Swapchain`'s private statics became
  one-line adapters that unpack `vk::SurfaceCapabilitiesKHR` and delegate.
- **`calcMipLevels`** (`aegis::rhi::detail`, in `:image`) — was a private static on `Image`. It
  never needed an image.

### Where an extraction lives

| Shape | Home | Why |
|---|---|---|
| A type with its own state and invariants | its own partition (`:ring_allocator`) | it has an identity beyond its first caller and is independently reusable |
| A stateless helper function | `aegis::rhi::detail` in its **owner's** partition | it has no meaning apart from that type, and `detail` marks it as not-API |

The `detail` namespace matters: these helpers are exposed so they can be tested, not because they
are public API. Putting them in `aegis::rhi` proper would widen the module's surface by accident.

Either way, leave a thin adapter at the Vulkan call site. A separate partition additionally gets
purity *enforced* — `:ring_allocator` imports no Vulkan, so a `vk::` parameter cannot creep in. A
`detail` helper inside a Vulkan-importing partition has no such guard; the test failing to compile
is what catches it.

### Gotcha: test-visible signatures cannot name `vk::` types

`aegis-rhi` links `vulkan-module` **PRIVATE** and `rhi.cppm` re-exports no Vulkan types, so a test
TU cannot name a `vk::` type at all — it cannot even construct an argument for one. Making a
Vulkan-taking helper `public` therefore does **not** make it testable.

Extracted helpers must take plain values. `chooseSwapchainExtent` takes `Extent2D` min/max/current
rather than a `vk::SurfaceCapabilitiesKHR`; only the unpacking stays in `Swapchain`.

It keeps Vulkan's "you pick the size" sentinel (`currentExtent == UINT32_MAX` on **both** axes) as
part of its contract, and trusts `currentExtent` only when neither axis is the sentinel — a
half-sentinel extent falls back to clamping rather than propagating `uint32_t::max` as a real size.

### Watch for partition cycles

`calcMipLevels` would naturally belong in `:utility`, but it takes `Extent3D` from `:common`, and
`common.cppm` already imports `:utility` — importing back would be circular. It went into `:image`
instead. Check the existing import direction before choosing a home for an extracted helper.

## Practices

- **One assertion focus per `TEST_CASE`;** use `SUBCASE` for variations that share setup rather
  than duplicating cases.
- **Name by behavior, not by method.** `TEST_CASE` text should read as an expectation, so a failure
  report states what broke.
- **Prefer `CHECK` over `REQUIRE`** unless a later assertion would crash on the failure — `CHECK`
  keeps going and reports every failure in the case.
- **Keep the suite fast and green.** It runs on every build; a slow or flaky unit test defeats the
  purpose.
- **A new test can double as a bug report — or a clean bill of health.** `FreeList::pop()` was
  suspected of being inverted; the first test pinned it as *correct* and closed the question.
- **Mirror new modules as they land.** When `render_graph` / `assets` arrive, add
  `tests/<module>/` with its own `CMakeLists.txt` in the same shape.

## Design notes

**Why doctest.** Minimal compile-time overhead (single header, ~4× faster to include than Catch2
v3) matters in a modules-heavy, warnings-as-errors build. Header-only, trivial single assertions,
and it ships CTest integration (`doctest_discover_tests`). Vendored as a submodule like every other
dependency, under `external/`'s `SYSTEM` subdirectory so its headers are immune to
warnings-as-errors.

**Why one executable per module.** Relink cost stays proportional to what changed, and each binary
links only its own library — editing an RHI test does not relink the whole engine's tests. `ctest`
still aggregates cases across every executable, so "run all" remains one command. Split finer only
if a single module's test link becomes slow.

**Why a shared `aegis-test-main`.** The doctest implementation compiles once into a static lib that
every test executable links, instead of being recompiled into each one.

**Why `CMAKE_CXX_SCAN_FOR_MODULES` is set here.** `tests/` sits outside the `src/aegis/` scope that
enables module scanning, so `CMakeLists.txt` in this directory turns it on explicitly.

Tests are gated behind `AEGIS_BUILD_TESTS` (defaults to on for a top-level build) in the root
`CMakeLists.txt`; doctest's CTest integration is included by `CMakeLists.txt` in this directory.
