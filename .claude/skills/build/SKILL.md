---
name: build
description: Configure and build an Aegis Engine preset and report the first real error.
disable-model-invocation: true
---

Build the preset in `$ARGUMENTS`; default `windows-clang-debug` on Windows, `linux-clang-debug` on
Linux. AGENTS.md "Build & run" has the presets and the pinned Vulkan SDK that `slangc` comes from.

1. Configure only if `build/<preset>/build.ninja` is missing or CMake files changed:
   `cmake --preset <preset>`.
2. Build: `cmake --build --preset <preset>`. Shaders compile as part of it.
3. Report whether it built clean. On a failure, give the **first** real error with file:line, not
   the downstream cascade.
