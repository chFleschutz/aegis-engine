# Agent notes: gotchas and workflow ideas

Collected during the RHI migration (Phases 0–6). These notes add to `AGENTS.md` and don't repeat it.
Read that file first.

## Tooling on this machine

- **`git clang-format` fails** with "Python was not found": git's shell hits the Microsoft Store
  `python` alias. Call the script directly from Bash:
  `python "/c/Program Files/LLVM/bin/git-clang-format" --staged`.
- **git-clang-format only sees staged changes.** Run `git add -A` first, or new files are skipped.
  After it reformats, re-add the files. One pass is enough.
- **Search with `git grep` or the Grep tool, not `grep -r`.** `build/` and `cmake-build-debug/` are
  gitignored but present, and `grep -r` returns thousands of hits from them.
- **Every `git add` prints "LF will be replaced by CRLF"** (`core.autocrlf=true`, and tools write LF).
  It's harmless; filter it with `grep -v "^warning"`.
- **PowerShell is 5.1, so there is no `&&`.** Use the Bash tool for git, grep, sed and python. Keep
  PowerShell for Windows-specific work: window capture and long-path deletes.
- **Long paths:** `git worktree remove` on a worktree under the scratchpad fails with "Filename too
  long". Use `Remove-Item -LiteralPath "\\?\C:\..." -Recurse -Force`, then `git worktree prune`.
- **The build needs Vulkan SDK 1.4.363.0.** `-DVulkan_ROOT=...` alone doesn't switch a configured
  build dir: the headers path and `SLANGC_EXE` stay cached. Use `--fresh`. A shell started before an
  SDK install keeps the old `VULKAN_SDK`; configure then fails with "Could NOT find Vulkan". Pass
  `-DVulkan_ROOT=` or export `VULKAN_SDK` explicitly.
- **Backslashes in Bash heredocs get mangled:** a doubled backslash in a Python heredoc arrives as a
  single one (so `'\\'` becomes an unterminated string). Write scripts that need backslashes to a
  file in the scratchpad instead.

## Build

- **Touching a `.cppm` interface recompiles every importer**, even if you only changed a comment
  (about 180 steps for `render_graph`). Put changes in the `.cpp` where you can, and batch interface
  edits before building.
- **Stale shader binaries.** Deleting or moving a `.slang` file leaves its `.spv` in
  `build/<preset>/shaders/`, and the loader will load it without complaint. After moving shaders,
  delete those outputs. (Editing a shader module used to leave stale binaries too; shaders now depend
  on every module.)
- **Test configure with `--fresh` after CMake changes.** A warm cache hides ordering bugs: `slangc` was
  looked up after the subdirectory that used it, and only a fresh configure showed it.
- **Release builds are a separate check.** `AGX_ASSERT*` compiles to nothing there, so code that only
  uses a variable in an assert can break the release build or change behaviour.
- **A background build that exits 0 is not proof.** Check that the artifact exists: one background
  build reported success, but its executables were never linked.
- **Don't background builds with `( … ) &` in Bash.** The subshell is killed when the call returns. Use
  the tool's `run_in_background` instead.

## C++ and modules

- **doctest with `-Werror`:** `CHECK(view == "literal")` does not compile for `std::string_view`.
  Compare against `std::string_view{ "literal" }`.
- **Don't capture `this` in callbacks stored in the frame graph** if the object moves afterwards.
  `Engine::create()` returns by value, so such a callback dangles and segfaults. Capture pointers to
  heap-owned members (`*m_renderer`, `*m_frameGraph`) instead.
- **`Engine` member order is destruction order.** Everything owning GPU resources must be destroyed
  before the device. Construct `platform::Input` before `ImGui_ImplGlfw_InitForVulkan`, so that
  ImGui's GLFW callbacks forward to it.
- **The facade's "log, assert, return `T{}`" helper needs a default-constructible `T`.**
  `render_graph::Ibl` isn't one, so it is created explicitly.

## Vulkan, ImGui and shaders

- **ImGui is built with `IMGUI_IMPL_VULKAN_NO_PROTOTYPES`.** Call `ImGui_ImplVulkan_LoadFunctions`
  before `ImGui_ImplVulkan_Init`. With prototypes, ImGui's calls went to another loader's global
  function pointers, and Simple-Scene crashed with a DEP violation.
- **`ImGui_ImplVulkan_LoadFunctions` fails if any loaded pointer is null.** A headless instance has
  no surface or swapchain functions, so `UiPass` gives those a non-null stub that aborts.
- **Run `spirv-val` on shader output when changing slangc versions.** SDK 1.4.328's slangc compiles
  some shaders into SPIR-V that validation rejects (see AGENTS.md, Shaders); a compile without errors
  proves nothing.
- **Keep the task shader in its own Slang file.** If the mesh and task stages share one module, the
  mesh stage also declares the task stage's `DrawIndex`, and validation rejects it.
- **GPU matrices are row-major:** upload `glm::rowMajor4(m)`. Every shader uses scalar layout, so C++
  structs match the Slang ones member for member (`shaders/modules/README.md`).

## Verifying

- **Examples have no pass/fail signal.** I checked them with a scratchpad PowerShell script, rewritten
  each session, that:
  - starts the exe and waits;
  - captures the window with `PrintWindow(hwnd, hdc, 3)` (`PW_RENDERFULLCONTENT` is required, because
    a plain capture of a Vulkan window comes out black);
  - closes it with `CloseMainWindow`;
  - counts `Vulkan … Error|Warning` lines on stderr.
- **Look at the screenshot after any render change.** Exit 0 with an empty stderr does not catch
  wrong output.
- **Check post-compaction summaries against the code.** After a context compaction, the summary
  claimed the GPU-driven instance limits had been raised, but the code was unchanged. Verify claims
  before writing them into docs.

## Workflow improvement ideas (most valuable first)

1. **Headless examples.** Have `Engine` accept `--headless --frames N --screenshot <png>` (the renderer
   already renders offscreen) and register each example as a `device` CTest. Examples would get a
   pass/fail signal, windows would stop popping up, and the capture script would no longer be needed.
2. **Reference images.** Store a PNG per scene and compare it with a tolerance in CTest. This catches
   rendering regressions that validation never reports.
3. **A Linux CI job** (GitHub Actions, Ubuntu + Clang + the LunarG SDK tarball) building everything
   and running `ctest -LE device`. Nothing else proves the Linux build; the MinGW proxy in AGENTS.md
   stops at `aegis.scene`.
4. **Check in the missing-include scanner** used for the Linux audit (a Python script mapping `std`
   names to headers per translation unit), and run it from CI. Mostly moot since the engine moved to
   `import std`: only the macro headers (`<cassert>`, `<cfloat>`, `<cstdio>`) are still included.
5. **Remove stale `.spv` files at configure time:** delete outputs in `build/<preset>/shaders/` that
   no longer have a source.
6. **Make `git clang-format` work:** disable the Store `python` alias, or add a `tools/format.sh`
   wrapper.
7. **Check the capture script into the repo** (for example `tools/run-capture.ps1`) if windowed checks
   remain necessary after idea 1.
8. **Finish converting the legacy modules** (`core`, `editor`, `scene`, `scripting`, `ui`). Formatting
   and naming already match the new style; what remains is `AGX_ASSERT` → `std::expected` and
   dotted submodules → partitions, best done as part of their redesign.
