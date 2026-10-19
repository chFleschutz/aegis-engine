# Engine application architecture: review and proposal

Status: **proposal**, not implemented. Written after the RHI migration (see `rhi-migration-plan.md`).

This document covers the application layer: `aegis::Engine` and the code it holds together (window,
GPU, assets, scene, scripting, editor, frame loop). The render stack beneath it (`aegis.rhi` →
`aegis.renderer` → `aegis.render_graph`) is in good shape and is not in question. It uses `Desc`
structs, `create()` returning `std::expected`, and dependencies passed in explicitly. The problem is
the layer above it.

## Problems in the current design

### 1. A global singleton reached through static accessors

`Engine` (`src/aegis/engine.cppm`) registers itself in `s_instance`. It exposes `instance()` plus 10
static accessors (`Engine::device()`, `Engine::scene()`, `Engine::renderer()`, …). The asset helpers
(`Engine::loadModel`, `loadTexture`, `loadEnvironment`, `createMaterial`) also go through the global.
Two more singletons exist alongside it: `core::AssetManager` (asserts there is only one) and
`Profiler::instance()`.

Why it hurts:

- **Hidden dependencies:** a function's signature doesn't show what it uses.
- **One engine per process:** only one can exist at a time.
- **Duplicated setup:** the engine can't be built in tests, so `aegis-test` (`src/aegis/test/main.cpp`)
  rebuilds the same setup in its own `Engine` class (context, device, renderer, frame graph, IBL).
  Two setup paths will drift apart.

### 2. Setup can't fail properly

The constructor asserts. `Engine::check()` logs, asserts, and then returns `T{}`. In release builds
`AGX_ASSERT_X` compiles to nothing (`src/aegis/core/assert.h`), so:

- a model that fails to load returns an empty entity and the engine carries on;
- a failed `DefaultAssets::create` leaves the defaults empty (null textures) and the engine carries on;
- `std::ignore = m_device->flushUploads()` throws away upload errors.

The layers below report errors with `std::expected<T, Error>`, and the engine discards those errors
at the top.

### 3. Configuration is compiled in

These are all constants:

- `Engine::ENABLE_GPU_DRIVEN_RENDERING`
- `core::DEFAULT_WIDTH` / `DEFAULT_HEIGHT`
- `core::ENABLE_FPS_LIMIT`
- the window title

Eval-Scene chooses its scene by commenting lines in and out. The renderer already supports headless
rendering (`Renderer::Desc::window = nullptr`), but no example can reach that mode. So examples have
no pass/fail signal, always open a window, and can't run in CTest. Changing any of these settings
means a rebuild, which with modules costs about a minute.

### 4. Correctness depends on member declaration order

`Engine` has 16 members. Their order is guarded by comments ("Order matters", "Before ImGui's GLFW
backend installs its callbacks") and by a destructor that resets some members early.

ImGui setup is spread over four places:

- `UI::UI` creates the context.
- `Engine::createFrameGraph` starts the GLFW backend.
- `~Engine` shuts that backend down.
- `render_graph::UiPass` owns the Vulkan backend.

### 5. The scene lifecycle is half-built

- **No real reset:** `Scene::reset()` is a TODO, so a second `loadScene` call adds the systems and
  default entities again.
- **Leaked scripts:** scripts live outside the scene in `ScriptManager`. They are created with `new`
  and never deleted, so they leak, and they are never reset.
- **Forced defaults:** `createDefaultScene` puts a camera with a movement script, lights and a sky into
  every scene. Examples then edit them through `scene.mainCamera()` and similar accessors.
- **Needless class:** `SceneDescription` is an abstract class with one method, where a plain function
  would do.

### 6. The new render code depends on the legacy scene code

- `render_graph::FrameInfo` carries an `aegis::scene::Scene&`.
- `aegis.assets` and `aegis.render_graph` import `aegis.scene` or `aegis.scene.registry` about 18 times.
- `DrawBatchRegistry` subscribes directly to entt's component signals.

So render passes read ECS components directly, and the render graph can't be driven without a legacy
`Scene`. The dependency points the wrong way: new code depends on legacy code.

### 7. Leftovers

- `core::AssetManager` stores the default assets under their old `default/...` names, and nothing reads
  them.
- The `core::Asset` base class ties `aegis.assets` to the legacy `aegis-core`.
- There are two profilers: the CPU `Profiler` singleton and `FrameGraph::timings()`.
- The whole engine is inline in the module interface `engine.cppm`, so any edit rebuilds every example.

## Proposal: build the engine the way the RHI is built

Use explicit objects, a `Desc`, `create() -> std::expected`, and pass dependencies in.

```
aegis.app (new-style module, replaces aegis.engine)
├─ AppDesc       title, extent, headless / frames / screenshot, gpuDriven {auto|on|off},
│                fpsLimit, editor on/off — defaults, overridden from the command line
├─ Engine        Engine::create(desc) -> std::expected<std::unique_ptr<Engine>, Error>
│  ├─ Platform      window + input; absent when headless
│  ├─ Gpu           context, device, renderer
│  ├─ AssetLibrary  defaults, IBL, loaders (replaces AssetManager and the static helpers)
│  ├─ World         scene registry, systems, scripts; reset() really clears
│  └─ Presentation  frame graph, draw batches, ImGui context + backends, optional editor
└─ run(argc, argv, AppDesc, scenes) -> int   parse CLI, create, load scene, loop, exit code
```

An example then becomes:

```cpp
auto buildSponza(aegis::app::SceneContext& ctx) -> std::expected<void, aegis::app::Error>
{
    auto sky = ctx.assets.loadEnvironment("Environments/KloppenheimSky.hdr");
    if (!sky)
        return std::unexpected{ sky.error() };
    ctx.world.setEnvironment(std::move(*sky));

    if (auto sponza = ctx.assets.loadModel(ctx.world, "Sponza/Sponza.gltf"); !sponza)
        return std::unexpected{ sponza.error() };
    return {};
}

auto main(int argc, char** argv) -> int
{
    return aegis::app::run(argc, argv, { .title = "Sponza" }, { { "sponza", buildSponza } });
}
```

### Why these choices

**Pass dependencies explicitly instead of using a global.**

- A scene function that takes `SceneContext&` shows exactly what it uses.
- Several engines can exist side by side, so tests can build a real one.
- `aegis-test` can use the same setup code instead of keeping its own copy.
- The codebase then follows one pattern (the RHI's) instead of two.

**Five services instead of 16 members.** Each service groups things that are created and destroyed
together:

- Gpu outlives everything that holds GPU resources.
- World can be reset without touching the GPU.
- Presentation is the one place that knows the ImGui and GLFW ordering rules.

The top-level order shrinks to five lines that mirror real dependencies (Platform → Gpu →
AssetLibrary → World → Presentation), and destruction order follows from it. Today's ordering traps
move inside the service that causes them and are documented there once.

**Return `std::expected` all the way up, and have `main` return an exit code.**

- Release builds stop running on with null objects.
- Errors keep the message the lower layers already produce.
- A non-zero exit code is what lets an example act as a test.

**Configure at runtime, through an `AppDesc` and command-line flags.** This is the biggest practical
gain:

- `--headless --frames 600 --screenshot <png>` turns every example into a CTest (label `device`) that
  needs no window, matching what `aegis-test` already does.
- Eval-Scene gets `--scene <name>` instead of commented-out lines.
- Switching the GPU-driven path no longer needs a rebuild.

**Scenes are named functions, and the World owns their scripts.**

- Scripts become `std::unique_ptr`s owned by the World, which fixes the leak.
- `reset()` can really clear the scene, which makes switching scenes at runtime possible (for
  example from the editor menu).
- `createDefaultScene` becomes an opt-in helper that a scene function calls, not something forced on
  every scene.

**Keep the existing concepts, but give each a clear owner.** Nothing new needs inventing:

- Layers (the editor) belong to the Engine and survive scene switches.
- ECS systems and scripts belong to the World and die with the scene.
- The order of the frame loop is defined in one function.

**Later: a render view between the scene and the render graph.** Introduce a plain
`render_graph::SceneView` (camera, lights, environment, instance batches) that a bridge in `aegis.app`
fills from the ECS. Static instances can be updated incrementally, as `DrawBatchRegistry` already does.

- Why:
  - It fixes the dependency direction: `render_graph` stops importing `aegis.scene`.
  - Render passes become testable without a scene, since `aegis-test` can hand them a `SceneView`
    directly.
  - It allows a separate extract step and render step, which could later run in parallel.
- Cost: one copy per frame, plus a bridge to maintain. It touches every pass, so it comes last.

### What stays

- The rhi → renderer → render_graph layering, and the frame graph with its declared resource usage.
- The ECS (`aegis::scene`), systems, per-entity scripts, and the layer concept for app-level tools.
- Convenience asset helpers. They become member functions of `AssetLibrary` instead of static ones.

## Suggested order (one commit each)

1. **Runtime configuration and error handling:** `AppDesc`, command-line flags, `Engine::create() ->
   std::expected`, and `main` returning an exit code. Register the examples as `device` CTests.
   Small change, biggest payoff.
2. **Remove the globals:** replace the static accessors with services passed to scene functions.
   Delete `AssetManager` and `core::Asset`. Move the implementation out of the module interface into
   `engine.cpp`.
3. **World:** owns its scripts (`std::unique_ptr`), `reset()` really clears, scenes are named
   (Eval-Scene becomes selectable).
4. **ImGui in one place:** move ImGui ownership into Presentation and make the editor optional.
5. **Scene view:** add `SceneView` extraction, so `render_graph` no longer imports `aegis.scene`.

## Open decisions

- **Module and namespace name:** `aegis.app` / `aegis::app` or `aegis.engine` / `aegis::engine`.
- **Code style:** whether `scene` and `scripting` move to the new code style in the same pass, or stay
  legacy for now.
- **Scene definitions:** plain functions (proposed) or a class like `SceneDescription`.
