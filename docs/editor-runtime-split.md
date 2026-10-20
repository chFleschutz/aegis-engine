# Editor/runtime split and game modules

Status: **proposal**, not implemented. It builds on `engine-architecture.md` (services, `Engine::create
-> std::expected`, runtime configuration, no singleton) and replaces two of its choices: scenes as
named C++ functions, and a `main()` per example.

> **The asset system is designed in [`asset-system.md`](asset-system.md)** (D8): sidecars, the import
> cache, `AssetRef`, cooking, and the World/Scene terminology with scenes as prefabs. This document
> fixes the boundaries it fits: assets referenced only by stable ID, one serializer for scenes and save
> games, a cook step for export.

## Problem

- **Every example is its own executable with its own `main()`.** Each one is a CMake target and a
  full link of the engine. A new game repeats the boilerplate.
- **The editor is part of every build.** `aegis-engine` links `aegis-editor` PUBLIC, `engine.cppm`
  does `export import aegis.editor`, and the `Engine` constructor pushes `EditorLayer`. A shipped game
  would carry the editor.
- **Scenes are code.** `SceneDescription::initialize` builds a scene by calling C++, so a scene can't
  be saved, edited in the editor and saved back, or loaded from a save game.

## Model: Unreal's structure, Godot's data model

How the big engines solve it:

- **Unreal** owns `main()` (the `Launch` module). A game is a module that registers itself
  (`IMPLEMENT_PRIMARY_GAME_MODULE`). The editor and the game are separate build targets
  (`MyGame.Target.cs` / `MyGameEditor.Target.cs`), with editor-only modules left out of game builds.
  In development the editor loads the game as a DLL; shipping links everything into one executable.
- **Godot** ships one engine binary for every game, because a game is data: scenes are serialized node
  trees, nodes are created by name through a reflection registry (`ClassDB`), and behavior is scripts.
  The editor is the same engine built with `TOOLS_ENABLED`; export pairs a prebuilt runtime without the
  editor with a `.pck` of the project.

Aegis takes **Unreal's build structure with Godot's data model**: game code stays C++, while scenes,
assets and project settings become data, connected through one type registry.

```
Engine (libraries)                                   Game
──────────────────────────────────────────────       ─────────────────────────────
aegis-launch    main(), CLI, startup/shutdown        game module (C++):
aegis-app       Engine, services, frame phases         components, scripts, systems
aegis-world     World, entities, built-in components   one registration function
aegis-reflect   TypeRegistry                         project folder (data):
aegis-assets    asset IDs, loading, serialization      manifest, scenes, materials,
aegis-script    C++ script host (later: a VM)          imported assets
renderer, render_graph, rhi, platform
aegis-editor    editor UI (only in the editor exe)
cmake           aegis_add_game()
```

Dependencies only point down: game → runtime, editor → runtime. No engine library depends on the
editor or on a game. The one game symbol the engine needs is named in the launcher's `main()`, which
is compiled into each game's executables rather than into an engine library.

## What the engine provides

**Entry point (`aegis-launch`).** This is the only `main()`, in two flavors built from the same code
(runtime and editor). It parses the command line (`--project <dir>`, `--scene <name>`, and the
`aegis-test` options `--headless`, `--frames`, `--screenshot`), calls `Engine::create`, calls the game's
registration, loads the project manifest and the startup scene, and runs.

**Services, passed explicitly.** The device, renderer, frame graph, window, input, assets, type
registry and worlds, as in `engine-architecture.md`. There is no `Engine::instance()`.

**Worlds.** `World` is a plain object, and several can exist at once (the edit world plus a play
copy, or a menu plus a level). Every entity has a **stable ID** (a UUID component), because
`entt::entity` indices are reused and can't be stored in a file. The built-in components (Transform
with hierarchy, MeshRenderer, Camera, lights, Environment, Script) are registered through the same
API a game uses.

**Type registry: the single extension point.** It is a runtime description of every type that can be
saved, shown in the editor or scripted:

```cpp
types.component<PointLight>("PointLight")
    .field("color", &PointLight::color)
    .field("intensity", &PointLight::intensity);
```

Its users are the serializer, the editor's inspector, undo (which records "field X of entity Y went
from A to B"), the cooker (fields of type `AssetId` are asset references) and, later, a scripting VM.
The API should be able to describe functions too, not only fields, so a VM can generate its bindings
from it. Don't build it on `entt::meta`: it crashes Clang `-O3` with libstdc++ (see the comments in
`scene.cppm`).

**Assets ([`asset-system.md`](asset-system.md)).** Fixed boundaries:

- Scenes, materials and scripts reference assets **only by ID**, never by path. Paths exist only
  inside the asset database. This also removes the absolute `PROJECT_DIR` paths.
- Importers turn source files (glTF, OBJ, PNG, HDR) into runtime assets, cached per project.
- Loading is by ID and returns reference-counted `AssetRef`s.

Each source file gets a `.meta` sidecar holding its asset ID and import settings (the Unity/Godot model,
which the prototype used and which survives moves and merges cleanly in git).

**Serializer.** It is generic and works only through the registry, so it saves game types it has
never heard of. A single format serves scene files (written by the editor), save games (the running
world) and play-in-editor (copying the edit world into a play world).

**Script host.** A script is a C++ class with fixed hooks (`onCreate`, `onUpdate`, `onDestroy`),
attached by **registered name**, with its persistent state in registered fields. Scripts get a
`ScriptContext`, the narrow gameplay API: this entity, get and add components, spawn, destroy, find by
ID, input, time, events and assets. A Lua or C# host later is a second implementation of the same
hooks, bound to `ScriptContext` and the registry.

**Frame phases.** Input → fixed update → update → late update → render preparation → render →
present. Systems register into a phase, and the editor gets a safe point between frames.

**Editor (`aegis-editor`, the editor executable only).**

- an asset browser
- the scene hierarchy
- an inspector **generated from the registry**, so new types need no UI code
- a viewport with gizmos
- save and load of scenes
- undo
- play-in-editor: serialize the edit world, run a copy, discard the copy

**Build support.** `aegis_add_game()` creates the targets, wires shaders and registers device tests.

## What a game does

1. **One CMake call.** It creates `<Name>` (runtime) and `<Name>-Editor`. The game's sources are
   compiled once into an OBJECT library (a static library would let the linker drop object files that
   nothing references):

   ```cmake
   aegis_add_game(Examples
       SOURCES src/rotator.cpp src/light_ring.cpp src/module.cpp
       PROJECT ${CMAKE_CURRENT_SOURCE_DIR}/project)
   ```

2. **Its types and one registration function:**

   ```cpp
   struct Rotator : aegis::Script
   {
       float speed{ 90.0f }; // registered: saved, shown in the inspector, scriptable

       void onUpdate(aegis::ScriptContext& ctx, float dt) override;
   };

   void registerTypes(aegis::TypeRegistry& types)
   {
       types.script<Rotator>("Rotator").field("speed", &Rotator::speed);
   }

   AEGIS_GAME_MODULE({ .name = "Examples", .registerTypes = registerTypes, .onStartup = onStartup })
   ```

   `AEGIS_GAME_MODULE` defines the one function the launcher calls. It must live in a small header,
   because macros don't cross module boundaries. Static linking makes it an ordinary function; a later
   hot-reload build would export it from a DLL, and game code wouldn't change.

3. **A project folder**, mostly written by the editor: a manifest, the project file
   `<Name>.aegis` (name, startup scene, settings), plus `scenes/` and `assets/`.

**Rules for game code:**
- no `main`, no Vulkan, no globals
- persistent state in registered fields
- entities referenced by ID or handle
- assets referenced by ID

**The examples become one game.** Simple, Sponza, Helmets and Eval become scene files in one project,
and Template becomes an empty project. Procedural content (Simple-Scene's 64 random lights) becomes a
script that spawns in `onCreate`. Each scene runs as a `device` CTest with `--headless`.

## Scripting language: planned for, not built

Everything a VM needs from the engine, the save/load requirement already forces:

- behavior attached by name, as data
- script state in registered fields
- stable entity IDs
- handles instead of pointers
- fixed lifecycle hooks

The only scripting-specific design point is that the registry should be able to describe functions.
Choosing a language (Lua via sol2 or LuaJIT, Luau, C#), embedding it and building debugger support wait
until a real game shows the iteration cost of C++. C++ logic stays supported either way.

## Reflection: manual now, automated later

Registration is explicit for now: one line per type and one per field. It works on every compiler, and
it is the place for editor metadata.

- **Automating the field lists today:** Boost.PFR or reflect-cpp can fill in the fields of
  *aggregates* behind the same `types.component<T>("Name")` call. That covers components. Scripts are
  not aggregates (they have virtual functions), and these libraries can't reflect fields split between
  a base and a derived class. To use them for scripts, keep a script's state in a nested aggregate
  (`struct Fields { … } fields;`). Keep heavy template libraries inside `.cpp` files, out of module
  interfaces.
- **C++26 reflection (P2996)** would generate everything, including private and base-class members,
  with annotations for editor metadata. As of October 2026, GCC 16.1 has it as experimental
  (`-freflection`). Mainline Clang does not have it; only Bloomberg's experimental fork does. MSVC has
  no support. The registry API stays the same, so the backend can switch once Clang supports it.
- **Keep the type names explicit even then.** The name is the save-file format; deriving it from the
  C++ class name would make renaming a class break every scene. The explicit call is also where aliases
  for renamed types and fields go.

## Exporting a game

The runtime executable is the shipped game. Each game ships its own runtime, because game code is
compiled in (Unreal's model, not Godot's). Still to add:

1. **Find data at runtime.** The launcher looks up `--project`, then data next to the executable, then
   the configure-time path (development builds only, compiled out of shipping builds). This applies to
   shaders too.
2. **A cook step:** `<Name>-Editor --cook <out>`. It has to link the game module, because scenes
   contain game types. Unreal and Godot also cook from the editor binary.
   - Importers run at cook time, so the runtime loads only cooked formats, and fastgltf, tinyobjloader
     and stb_image can leave the shipped executable.
   - The `ID → file` lookup becomes a prebuilt `ID → pak offset` table behind the same interface.
   - What to cook: walk the manifest's scenes and follow `AssetId` fields found through the registry,
     plus an "always include" list for assets that only code loads.
3. **Packaging:** `install()` rules and CPack in `aegis_add_game`, a release build with a shipping
   define (no validation layers, no developer overrides), and the GUI subsystem on Windows. CI builds
   each platform natively.
4. **Save games go to the user's data folder** (`%APPDATA%`, `$XDG_DATA_HOME`), never next to the
   executable.

## Lessons from the prototype

A throwaway prototype (plain headers, no GPU, text files for assets, a command-prompt editor) checked
the structure: one engine, two games, runtime and editor executables, save/load, play-in-editor,
undo. It surfaced two points:

- **Persistent script state must be a registered field.** A light-spawning script spawned a second
  ring after a save game was loaded: "already spawned" was runtime-only state, so `onCreate` ran
  again. A registered `spawned` field fixed it.
- **World time must be saved.** A loaded save restarted the clock at zero, so time-based motion
  jumped. A real world needs a saved clock, or systems that work incrementally.

## Impact on the backlog (M6)

M6 should be reworked before it starts. These items would otherwise be built and then replaced:

- **6.1** `aegis_add_example` (one executable per example) becomes `aegis_add_game` plus project
  folders.
- **6.3** named scenes as code (`--scene` selecting a function) become scene files.
- **"Component table with serialization"** is listed as deferred. Here it is the foundation, so it moves
  into M6, before **6.5** (the editor), or the editor gets built twice.
- **6.2** (removing the singleton) stays, extended to allow several `World` instances.
- The **"F1 follow-up"** (install rules once the game-usage model is decided) is decided by this
  document.

Suggested order:

1. Remove the singleton; make `World` a plain object (6.2).
2. The type registry: pure logic, unit-tested with doctest.
3. Scene and project serialization with stable entity IDs and asset IDs.
4. `aegis_add_game` and the engine-owned launcher, replacing 6.1's per-example targets.
5. The editor on top (6.5): registry-driven inspector, undo, play-in-editor.
6. Later: the export step, the DLL and hot-reload build, a scripting language.

## Open decisions

- **Text format: decided,** JSON through glaze (`asset-system.md`, "Formats and libraries"). The
  asset system's remaining open points are in [`asset-system.md`](asset-system.md), "Open".
- **Reflection backend** for automatic field lists: none, glaze (already a dependency for JSON; it
  reflects aggregate member names, with the same aggregate-only limit as Boost.PFR), reflect-cpp, or
  wait for C++26. Type names stay explicit either way.
- **Shape of script state:** fields directly on the script class, or a nested aggregate (needed for
  library-based reflection).
- **Entity hierarchy and prefab overrides:** prefabs are scenes and instances get derived entity IDs
  (`asset-system.md`); how the hierarchy serializes and how an instance stores overrides is open.
- **When to add** the DLL and hot-reload build, and a scripting language.
