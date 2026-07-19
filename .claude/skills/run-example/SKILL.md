---
name: run-example
description: Build and launch an Aegis Engine example (default Template-Scene) for a visual smoke test. Use to confirm the engine still renders after a change.
disable-model-invocation: true
---

Build and run one of the example executables to verify the engine renders. This is the primary
**device-level** end-to-end check — pure, GPU-independent logic is covered by the doctest unit
tests instead (`ctest --preset windows-clang-debug`; see `test-setup.md`), so use this to confirm
anything touching a live `vk::Device` still renders.

Example target (from `$ARGUMENTS`, default `Template-Scene`; valid: `Template-Scene`, `Simple-Scene`,
`Sponza`, `Helmets`, `Eval-Scene`). Map the target to its example dir (lowercased): e.g.
`Simple-Scene` → `examples/simple-scene`.

1. Build the target (warnings are errors — a clean build is part of the check):
   `cmake --build --preset windows-clang-debug --target <Target>`
2. Launch: `build\windows-clang-debug\examples\<dir>\<Target>.exe`
3. Report whether it built clean and the window opened / rendered. On build failure, surface the
   first real error (skip downstream cascade). `Template-Scene` is empty and safest; content scenes
   need the `aegis-assets` submodule checked out.
