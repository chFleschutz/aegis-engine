# Agent notes: gotchas

Gotchas from past sessions, to read when a build, tool or test behaves unexpectedly. They add to
`AGENTS.md` and don't repeat it.

## Tooling on this machine

- **Search with `git grep` or the Grep tool, not `grep -r`.** `build/`, `cmake-build-debug/` and
  `.claude/worktrees/` are gitignored but present, and `grep -r` returns thousands of hits from them.
- **PowerShell is 5.1, so there is no `&&`.** Use the Bash tool for git, grep, sed and python. Keep
  PowerShell for Windows-specific work: window capture and long-path deletes.
- **Long paths:** `git worktree remove` on a worktree under the scratchpad fails with "Filename too
  long". Use `Remove-Item -LiteralPath "\\?\C:\..." -Recurse -Force`, then `git worktree prune`.
- **The build needs Vulkan SDK 1.4.363.0.** `-DVulkan_ROOT=...` alone doesn't switch a configured
  build dir: the headers path and `SLANGC_EXE` stay cached. Use `--fresh`. A shell started before an
  SDK install keeps the old `VULKAN_SDK`; configure then fails with "Could NOT find Vulkan". Pass
  `-DVulkan_ROOT=` or export `VULKAN_SDK` explicitly. A configure that failed the SDK check can make
  the next `--fresh` fail once in compiler detection ("CMAKE_C_COMPILER not set"); run it again.
- **Backslashes in Bash heredocs get mangled:** a doubled backslash in a Python heredoc arrives as a
  single one (so `'\\'` becomes an unterminated string), and the call can fail with "unexpected EOF
  while looking for matching `'`". Write scripts that need backslashes to a file in the scratchpad
  instead, or edit with the Edit tool.
- **clangd needs `--experimental-modules-support`.** The root `.clangd` points clangd at
  `build/windows-clang-debug`'s compile commands but strips the build's `-fmodule-file` and
  `-fmodule-output` flags, so clangd builds its own BMIs (the first file takes 40–90 s). A clangd
  that loads the build's BMIs keeps them mapped, and on Windows the next build then fails with "a
  file with a user-mapped section open" until that clangd exits. Without the flag, each module unit
  shows one `module '…' not found`: noise. The Claude Code clangd plugin starts clangd without it,
  and doesn't map `.cppm`, so interface units get no diagnostics there;
  `clangd --experimental-modules-support --check=<file>` parses one by hand (its error count includes
  failed refactoring self-tests, `tweak: … FAIL`; only lines like `[unknown_typename] Line 1: …`
  are diagnostics).
- **clangd on Linux:** `.clangd` names the Windows preset and can't tell the OS apart (`If:` matches
  paths only), and it beats `--compile-commands-dir`. Set `CompileFlags: CompilationDatabase:
  <absolute path>/build/linux-clang-debug` in `~/.config/clangd/config.yaml`, which wins over
  `.clangd` (not tried yet).
- **A `cd` in a Bash call carries into the next call.** Later commands then run in that folder; use
  `git -C <dir>` or absolute paths instead.

## Build

- **Touching a `.cppm` interface recompiles every importer**, even if you only changed a comment
  (about 180 steps for `render_graph`). Put changes in the `.cpp` where you can, and batch interface
  edits before building.
- **Test configure with `--fresh` after CMake changes.** A warm cache hides ordering bugs: `slangc` was
  looked up after the subdirectory that used it, and only a fresh configure showed it.
- **Release builds are a separate check.** `assert` compiles to nothing there, so code that only
  uses a variable in an assert can break the release build or change behaviour.
- **A background build that exits 0 is not proof.** Check that the artifact exists: one background
  build reported success, but its executables were never linked.
- **Don't background builds with `( … ) &` in Bash.** The subshell is killed when the call returns. Use
  the tool's `run_in_background` instead.
- **MSVC (`windows-msvc-*`)** needs a Visual Studio developer shell (`VSCMD_VER` set). MSVC 14.50
  (VS 18.5) compiles `rhi`, `renderer` and `assets` but hits internal compiler errors (C1001) in
  `render_graph` and its tests. It also needs `<compare>` in every translation unit that instantiates
  a defaulted `operator<=>` (Vulkan-Hpp's `vk::Flags` included), and resolves `std::pair` structured
  bindings in templates at the instantiating TU.

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
- **Keep the task shader in its own Slang file.** If the mesh and task stages share one module, the
  mesh stage also declares the task stage's `DrawIndex`, and validation rejects it.
- **Upload matrices as they are.** `aegis.math` and every shader store matrices row-major, so a
  `math::Float4x4` goes into a GPU struct unchanged (`math::Float3x4{ model }` keeps the top three
  rows); a transpose on the way up is a bug. Every shader uses scalar layout, so C++ structs match
  the Slang ones member for member (`shaders/modules/README.md`).
- **glm habits don't compile on `aegis.math`, on purpose.** `m[c][r]` is `m[r, c]` (row first),
  `mat4{ 1.0f }` is `Float4x4::identity()`, `quat{ w, x, y, z }` is `Quat::fromXyzw(x, y, z, w)`, and
  `v * M` doesn't exist. Column-major interop converts at the boundary: ImGuizmo gets
  `std::bit_cast<std::array<float, 16>>(math::transpose(m))` and the result comes back the same way
  (`aegis/editor/viewport.cpp`).

## Verifying

- **Windowed runs: `tools/capture-window.ps1`.** It starts the exe (or attaches to a running one with
  `-Name`), captures the window with `PrintWindow(hwnd, hdc, 3)` (`PW_RENDERFULLCONTENT` is required,
  because a plain capture of a Vulkan window comes out black), closes it with `CloseMainWindow` and
  lists stderr's `[error]` lines. A planted file can stage what needs clicks: a copy of a scene in
  `<project>/.aegis/editor/autosave/<scene ID>.scene`, newer than the scene, shows the recovery bar.
- **Look at the screenshot after any render change.** Exit 0 with an empty stderr does not catch
  wrong output.
- **Check post-compaction summaries against the code.** After a context compaction, the summary
  claimed the GPU-driven instance limits had been raised, but the code was unchanged. Verify claims
  before writing them into docs.

## Open idea

- **Finish converting `scripting` and `world`.** Both are partitions and in the new style; what
  remains is `assert` → `std::expected` where misuse is recoverable, best done as part of their
  redesign.
