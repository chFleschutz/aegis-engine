# Findings, lens b (bugs): content and platform

Scope: `src/aegis/assets`, `src/aegis/platform`, and their consumers (`engine.cppm`, `test/main.cpp`,
`render_graph/ibl.cpp`, `tests/assets`, `examples/`, plus the scripting and editor code that reads
`platform::Input`). Read-only analysis; nothing was built or run. Bundled glTF files in
`modules/aegis-assets` were inspected as JSON.

Counts: 15 findings. By severity: 8 bug, 3 design problem, 4 polish. By confidence: 11 high, 4 medium.

### B-CONTENT-01: MaterialInstance / MaterialTemplate validation is assert-only; release builds corrupt memory
- **Where:** `assets/material.cpp`: `MaterialInstance::setParameter`, `MaterialInstance::parameter`,
  `MaterialTemplate::addParameter`, `detail::writeParameters`; `assets/material.cppm`:
  `MaterialInstance::parameter<T>`
- **Observed:**
  - `setParameter` only does `assert(parameter && ...)` and
    `assert(parameter->defaultValue.index() == value.index() ...)`, then always calls
    `m_overrides.insert_or_assign(name, std::move(value))`.
  - `writeParameters` writes each override with `std::memcpy(dst, &v, sizeof(T))`, where `T` is the
    *override's* type, at the template's offset.
  - `update` serializes into `std::vector<std::byte> block(m_blockStride)`.
  - `parameter(name)` dereferences `m_template->parameter(name)` unchecked. `parameter<T>` uses
    `std::get<T>`, which throws.
  - `addParameter` asserts uniqueness. In release, `emplace` silently drops a duplicate, but
    `m_parameterSize` still grows.
  - A null `shared_ptr<Texture>` override passes both asserts. It is written as `0xFFFFFFFF` indices.
    `shaders/*/geometry.slang` then samples `material->albedoMap.sample(...)` with no validity check.
- **Inference:** the PBR template is 76 bytes (albedo@0, emissive@12, metallic@24, roughness@28,
  ambientOcclusion@32, maps@36..68, 8 bytes each), so the block stride is 80. In a release build:
  - `setParameter("emissiveMap", glm::vec4{...})` writes 16 bytes at offset 68. That runs 4 bytes past the
    80-byte heap block: a heap overflow on every `update`.
  - `setParameter("roughness", glm::vec4{...})` overwrites `ambientOcclusion` and the albedoMap indices,
    and the shader then reads a garbage descriptor index.
  - An unknown name is silently dropped by `writeParameters`, but `parameter(name)` on it is a null
    dereference.
  - A null texture override makes the shader index the bindless array at `0xFFFFFFFF`. That is undefined
    behaviour, probably a device loss.
  Material values come from hard-coded name strings (`GltfImport::loadMaterials`), editor panels and
  scripts. A typo, or a template change (for example adding an alpha channel to `albedo`), turns into
  memory corruption instead of an error.
- **Severity:** bug
- **Confidence:** high
- **Affects:** every new material parameter or template, editor material editing, glTF material mapping.
  Unit-testable today: `detail::writeParameters` is exported.
- **Verify by:** a unit test calling `detail::writeParameters` on the PBR layout with a vec4 override
  for `emissiveMap` into an exactly `parameterSize()`-byte span, run under ASan in release; or code reading.
- **Fix sketch:** `setParameter` and `addParameter` return `std::expected<void, Error>` (unknown name, type
  mismatch, null texture). `writeParameters` copies `parameter.size` bytes. `parameter` returns a
  pointer or `expected`.

### B-CONTENT-02: COLOR_0 stored as VEC4 aborts debug builds and only works by luck in release
- **Where:** `assets/gltf_loader.cpp`, `GltfImport::loadMeshes` (`read3(*color, input.colors)`);
  `external/fastgltf/include/fastgltf/tools.hpp`, `iterateAccessor` (line ~689)
- **Observed:** `COLOR_0` goes through the POSITION/NORMAL helper,
  `fastgltf::iterateAccessor<fastgltf::math::fvec3>`. fastgltf asserts
  `accessor.type == Traits::type && "The destination type needs to have the same AccessorType as the accessor."`
  The glTF spec allows `COLOR_0` as VEC3 or VEC4. VEC4 (often normalized u16) is what Blender and most
  exporters write.
- **Inference:** in a debug build, loading any model with a VEC4 `COLOR_0` aborts inside the loader, and
  no `Error` is returned. In release the assert is compiled out. The stride comes from the accessor's own
  type, so the first three components are read correctly by accident. None of the bundled models has
  `COLOR_0`, so nothing has hit this yet.
- **Severity:** bug
- **Confidence:** high
- **Affects:** model import, and every future attribute read that copies `read3`: TANGENT is VEC4, and
  JOINTS/WEIGHTS too.
- **Verify by:** `Loader::load` on Khronos `VertexColorTest` in a debug build (add it to the
  `aegis-test` model list); or code reading.
- **Fix sketch:** dispatch on `accessor.type`, and return `InvalidData` for an unexpected type instead of
  asserting. A small checked `readAttribute<T>` helper does it.

### B-CONTENT-03: glTF images embedded as data URIs fail the whole load
- **Where:** `assets/gltf_loader.cpp`, `GltfImport::loadTextures` (the `std::visit` over `image.data`)
- **Observed:** the visitor handles `sources::URI` and `sources::BufferView`. Any other source returns
  `UnsupportedFormat` ("Unsupported glTF image source"). fastgltf always decodes a `data:...;base64,` image
  URI into `sources::Array` (`fastgltf.cpp`, `Parser::decodeDataUri` ~716, called for images ~2461).
- **Inference:** a `.gltf` exported as "glTF Embedded" has every image as a data URI. This is a common
  exporter option and a standard Khronos sample variant. Such a file fails with `UnsupportedFormat` at the
  first texture, and nothing of the model loads, even though the encoded bytes are already in memory in
  the type the BufferView branch reads.
- **Severity:** bug
- **Confidence:** high
- **Affects:** model import, and any future image source (KTX2, `Options::LoadExternalImages`).
- **Verify by:** `Loader::load` on `BoxTextured/glTF-Embedded/BoxTextured.gltf` returns `UnsupportedFormat`.
- **Fix sketch:** add a `sources::Array` branch. Fold it and the BufferView branch into one "encoded
  bytes → `loadFromMemory`" helper.

### B-CONTENT-04: glTF material semantics are dropped; bundled Sponza renders wrong
- **Where:** `assets/gltf_loader.cpp`, `GltfImport::loadMaterials` / `loadTextures`; `rhi/pipeline.cppm`
  (`CullMode cullMode{ CullMode::Back }`); `shaders/{cpu,gpu}-driven/geometry.slang`
- **Observed:**
  - The loader never reads `alphaMode`, `alphaCutoff`, `doubleSided`, `baseColorFactor[3]`
    (`glm::make_vec3` drops alpha), `texture.samplerIndex`, `textureInfo.texCoord`,
    `normalTexture.scale`, `occlusionTexture.strength` or `KHR_texture_transform`.
  - No geometry shader has `discard`. Only `post_effects.cpp` and `screen_passes.cpp` set a `cullMode`,
    so the geometry passes use the default, Back.
  - Every texture gets the default sampler (Repeat, linear).
  - `formats[textureIndex]` is last-writer-wins, so a texture used for both a color and a data role
    becomes sRGB for both.
  - Bundled content: `Sponza.gltf` has `alphaMode` MASK materials and 3 `doubleSided` materials.
    `MetalRoughSpheres.gltf` uses CLAMP_TO_EDGE (33071) samplers.
- **Inference:** in Sponza (Sponza and Eval-Scene examples), MASK materials (foliage, chains) render as
  opaque quads, and back faces of the doubleSided materials are culled. MetalRoughSpheres samples
  with Repeat, so edge texels bleed. Validation does not flag any of this. It is wrong output that only a
  screenshot comparison would catch. `MaterialType::Transparent` exists, but the loader never sets it.
- **Severity:** bug
- **Confidence:** high (that these fields are ignored, and the cull default); medium for how visible each
  artifact is
- **Affects:** model import, PBR template and shader struct (`common.slang` `PbrMaterial`), pass/pipeline
  selection per material, and every transparency or alpha-test feature.
- **Verify by:** `aegis-test --headless --scene --screenshot` with Sponza; or code reading plus
  inspecting the glTF JSON.
- **Fix sketch:** map alphaMode/cutoff/doubleSided into template parameters (plus cull state per batch),
  map glTF samplers to `Sampler::Desc`, and keep `baseColorFactor` as vec4. Log the unsupported
  ones (texCoord≠0, texture transform).

### B-CONTENT-05: primitive topology is ignored; strips, fans, lines and points are read as triangle lists
- **Where:** `assets/gltf_loader.cpp`, `GltfImport::loadMeshes`; `assets/mesh_preprocessor.cpp`, `validate`
- **Observed:** `primitive.type` is never read. Every primitive goes to `MeshPreprocessor::process`, whose
  only topology check is `indexCount % 3 != 0`.
- **Inference:** take a TRIANGLE_STRIP of 6 indices. It loads "successfully" as 2 unrelated triangles
  instead of 4. With a count that is not a multiple of 3, the whole model fails with `InvalidData`. LINES
  and POINTS primitives (common as helpers in CAD exports) become garbage triangles or sink the load.
  No error ever says that the topology is unsupported.
- **Severity:** bug
- **Confidence:** high
- **Affects:** model import; meshlet building assumes triangle lists.
- **Verify by:** code reading. Testable once glTF-to-`Input` is a pure step (see Notes for lens a).
- **Fix sketch:** convert strips and fans to lists in the loader. Skip non-triangle primitives with a
  warning, or return an `UnsupportedFormat` that names the primitive.

### B-CONTENT-06: every node in the file is instantiated, not the selected scene's tree
- **Where:** `assets/gltf_loader.cpp`, `GltfImport::run` / `buildScene`
- **Observed:** `sceneIndex = m_gltf.defaultScene.value_or(0)` is used only for the root's name.
  `buildScene` creates an entity for every `m_gltf.nodes[i]` and attaches every parentless one to the
  root. `scenes[sceneIndex].nodeIndices` is never read.
- **Inference:** a file with several scenes (variants, LODs, prop libraries) renders all of them on top
  of each other. Nodes that no scene references also appear at the origin.
- **Severity:** bug
- **Confidence:** high
- **Affects:** model import, future prefab or scene selection.
- **Verify by:** load Khronos `MultipleScenes`; both scenes appear. Or code reading.
- **Fix sketch:** walk recursively from `scenes[sceneIndex].nodeIndices`, creating entities only for the
  nodes it reaches.

### B-CONTENT-07: polled input ignores ImGui focus; typing in the editor moves the camera and switches gizmos
- **Where:** `editor/editor_layer.cppm`, `EditorLayer::onUpdate`;
  `scripting/movement/kinematic_movement_controller.cppm`, `applyRotation` / `applyMovement`;
  `platform/input.cpp`, `Input::keyPressed` / `mouseButtonPressed`
- **Observed:** `Input` polls GLFW directly (`glfwGetKey`, `glfwGetMouseButton`). `WantCaptureKeyboard`
  and `WantCaptureMouse` appear nowhere in the codebase. `EditorLayer::onUpdate` switches gizmo mode on
  Q/W/E/R through `m_input.keyPressed`. The scene panel has
  `ImGui::InputText("Entity Name", &nameComponent.name)`.
- **Inference:** renaming an entity to, say, "Wall" switches the gizmo to translate on "W". If W is bound
  to forward movement, the camera also moves while you type. Right-dragging inside an ImGui panel also
  rotates the camera and disables the cursor. Every future key-driven tool inherits this.
- **Severity:** bug
- **Confidence:** high
- **Affects:** the editor, the camera controllers, every new hotkey. `platform` has no notion of input
  capture.
- **Verify by:** run Simple-Scene, select an entity and type "wer" into its name field. Or code reading.
- **Fix sketch:** let the engine (which owns ImGui) set a per-frame "UI has keyboard/mouse" flag on
  `Input`, or on a thin wrapper, that `keyPressed`/`mouseButtonPressed` respect. `platform` stays free of
  ImGui.

### B-CONTENT-08: mouse-look sensitivity depends on frame rate
- **Where:** `scripting/movement/kinematic_movement_controller.cppm`, `applyRotation`
- **Observed:** cursor deltas are added to `rotate`
  (`rotate.x -= (cursorPos.y - m_previousCursorPos.y) * m_mouseSensitivity`) and then scaled with the key
  rotation: `rotate *= deltaSeconds`.
- **Inference:** a mouse delta is already a per-frame displacement. Multiplying it by the frame time makes
  the same hand movement turn the camera 4× less at 240 FPS than at 60 FPS. It also jitters with
  frame-time variance (busy-wait FPS limit on or off, scene load spikes).
- **Severity:** bug
- **Confidence:** high
- **Affects:** camera controllers, and any future consumer of cursor deltas. A pure
  `rotationFromInput(keys, delta, dt)` helper could be unit-tested.
- **Verify by:** code reading; or toggle `core::ENABLE_FPS_LIMIT` and compare how fast the camera turns.
- **Fix sketch:** apply `deltaSeconds` only to the key-driven part. Add the mouse delta × sensitivity
  unscaled.

### B-CONTENT-09: valid models without normals are rejected outright
- **Where:** `assets/gltf_loader.cpp`, `GltfImport::loadMeshes` ("lacks POSITION or NORMAL");
  `assets/obj_loader.cpp`, `Loader::loadObj` ("has a vertex without a normal");
  `assets/mesh_preprocessor.cppm`, `Input::normals` ("Required")
- **Observed:** a glTF primitive or OBJ vertex without a normal makes the whole load return
  `InvalidData`. The glTF spec says NORMAL is optional: "client implementations MUST calculate flat
  normals".
- **Inference:** this is reported cleanly, not a crash. But a spec-valid file (many OBJs, scanned or
  procedural glTF) cannot be loaded, and one normal-less primitive sinks a whole multi-mesh file. Normals
  are derived data that the pure, unit-tested `MeshPreprocessor` could compute.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** both loaders; `MeshPreprocessor` tests ("generates flat normals").
- **Verify by:** unit test: `MeshPreprocessor::process` with empty `normals` returns InvalidData.
- **Fix sketch:** make `normals` optional in `Input` and generate flat normals before dedupe. Drop the
  two loader checks.

### B-CONTENT-10: Window owns process-global GLFW state; a second window or a move-assignment leaves dangling GLFW windows
- **Where:** `platform/window.cpp`: `Window::Window`, `~Window`, `operator=(Window&&)`
- **Observed:**
  - The constructor calls `glfwInit()` without checking the result and sets no `glfwSetErrorCallback`.
    A failed `glfwCreateWindow` is only `assert(m_window && ...)`.
  - The destructor calls `glfwDestroyWindow(m_window); glfwTerminate();`.
  - Move-assignment swaps, "Swapping hands this window's old GLFW window to 'other', which destroys it".
- **Inference:** `glfwTerminate` destroys *every* GLFW window and resets the library.
  - (a) `window = platform::Window{desc}` (recreating a window): the temporary's destructor terminates
    GLFW, so `window.m_window` dangles immediately and the next `glfwPollEvents` is a use-after-free.
    The swap also hands the bound `Input`'s key handler to the temporary, so key bindings silently stop.
  - (b) Two live windows (tool window, multi-viewport): destroying the first kills the second.
  - (c) No display (Linux without X11/Wayland): `glfwInit` fails silently, and in release
    `glfwSetWindowUserPointer(nullptr, ...)` crashes. GLFW's own reason is never printed.
  No current code move-assigns a `Window`, so (a) and (b) are latent. (c) hits every example on a headless
  machine.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** headless or CI examples (engine-architecture.md step 1), any multi-window or editor
  feature, and `Window::create() -> expected` to match the RHI style.
- **Verify by:** code reading. A `device`-labelled check that creates two windows sequentially and
  overlapping.
- **Fix sketch:** a refcounted GLFW library guard (init once with an error callback, terminate with the
  last user). `Window::create` returns `std::expected`. Delete move-assignment, or fix it to destroy
  only its own window.

### B-CONTENT-11: Input::bind has no unbind and dispatch is not reentrancy-safe
- **Where:** `platform/input.cpp`, `Input::bind` / `Input::onKey`
- **Observed:** bindings are appended to `m_bindings[key]` (a `std::vector<Binding>`) and never removed.
  There is no unbind or handle. `onKey` does a range-for over `bindings->second` while calling
  `binding.callback()`.
- **Inference:** a callback that calls `bind` for the same key (a "rebind" UI, a toggle that installs its
  inverse) reallocates the vector mid-iteration, which is undefined behaviour. A binding that captures a
  script or layer `this` outlives it. Today scripts are leaked, never destroyed, which hides this. Once the
  World owns scripts and `reset()` destroys them (engine-architecture.md step 3), a key press after a
  scene switch calls into a destroyed object. No in-tree code calls `bind` yet, except `aegis-test`.
- **Severity:** design problem
- **Confidence:** medium
- **Affects:** scripting hotkeys, editor shortcuts, scene switching. The binding table is pure logic that
  could be unit-tested if lifted out of `Input(Window&)`.
- **Verify by:** a unit test on an extracted `KeyBindings` value type (bind inside a callback, unbind
  after destruction).
- **Fix sketch:** `bind` returns a scoped `Binding` handle (RAII unbind). Dispatch over a snapshot or
  index loop.

### B-CONTENT-12: Texture::create(desc, mip0) does not check the byte count; release reads out of bounds
- **Where:** `assets/texture.cpp`, `Texture::create(Device&, const Desc&, std::span<const std::byte>)`;
  `rhi/upload_manager.cpp`, `UploadManager::upload(ImageHandle...)`
- **Observed:** the assets overload forwards `mip0` unchecked. The only check is in the RHI:
  `assert(data.size() == last.sourceOffset + last.size && "Upload data does not match image layout")`,
  followed by `std::memcpy(... data.data() + footprint.sourceOffset, footprint.size)`.
- **Inference:** a public caller that passes RGBA8 bytes for an RGBA32F desc, or forgets the 6 cube faces,
  aborts in debug. In release, `memcpy` reads past the caller's buffer and uploads garbage. Current
  in-tree callers compute sizes correctly. The risk is in the next one (compressed or KTX formats, cube
  loaders).
- **Severity:** polish
- **Confidence:** medium
- **Affects:** new texture sources and formats. Testable if the expected byte count is a pure helper
  (`subresourceFootprints` already is).
- **Verify by:** code reading.
- **Fix sketch:** compare `mip0.size()` with the footprint total in `Texture::create`, and return
  `InvalidData`.

### B-CONTENT-13: meshlet limits are configurable on the CPU but hard-coded in the mesh shader and in uint8 fields
- **Where:** `assets/mesh_preprocessor.cppm`, `Input::maxVerticesPerMeshlet` / `maxTrianglesPerMeshlet`;
  `assets/mesh_data.cppm`, `Meshlet::vertexCount` / `primitiveCount` (`std::uint8_t`);
  `shaders/gpu-driven/geometry.slang` (`MAX_VERTICES = 64`, `MAX_PRIMITIVES = 126`,
  `SetMeshOutputCounts(meshlet.vertexCount, meshlet.primitiveCount)`)
- **Observed:** the limits are public `Input` fields without validation, and `process` casts the counts
  to `uint8_t`.
- **Inference:** a caller tuning `maxTrianglesPerMeshlet = 128` produces meshlets that exceed the mesh
  shader's declared maximum (undefined behaviour or a validation error). At 256 vertices the count wraps
  to 0. The knobs look supported but only one value is valid.
- **Severity:** polish
- **Confidence:** medium
- **Affects:** meshlet tuning, the GPU-driven path. A unit test could check the bounds.
- **Verify by:** code reading.
- **Fix sketch:** make the limits shared constants (one source for C++ and the shader), or validate them
  in `validate()`.

### B-CONTENT-14: RGBA32_SFLOAT textures are sampled with a linear filter without a format-feature check
- **Where:** `assets/texture.cpp`, `Texture::solidColor` / `solidColorCube` / `loadEquirectangular`;
  `rhi/sampler.cppm`, `Sampler::Desc` (`magFilter{ Filter::Linear }`, `minFilter{ Filter::Linear }`)
- **Observed:** the defaults and the equirect map are `RGBA32_SFLOAT` with the default linear sampler.
  `supportsMipGeneration` checks `eSampledImageFilterLinear`, but only for mip generation, never for
  sampling.
- **Inference:** `SAMPLED_IMAGE_FILTER_LINEAR` is optional for 32-bit float formats in Vulkan. On a device
  without it, every default texture and the HDR source violate VUIDs (magFilter-04553) and give undefined
  results. Desktop GPUs support it, so this is a portability issue.
- **Severity:** polish
- **Confidence:** medium
- **Affects:** portability, and new float texture formats.
- **Verify by:** code reading. On a device lacking the feature, validation fires.
- **Fix sketch:** use RGBA8 or RGBA16F for the 1×1 defaults, or pick nearest filtering when the feature
  is missing.

### B-CONTENT-15: format dispatch on file extension is case-sensitive
- **Where:** `assets/loader.cpp`, `Loader::load` (`extension == ".gltf" || extension == ".glb"`,
  `== ".obj"`); `assets/texture.cpp`, `Texture::loadFromFile` (`path.extension() == ".hdr"`)
- **Observed:** comparisons are exact-case.
- **Inference:** `Model.GLB` or `MESH.OBJ` (common from Windows tools) returns `UnsupportedFormat`.
  `sky.HDR` passed to `loadFromFile` skips the "use loadEquirectangular" guard: `stbi_load` tone-maps it to
  LDR, and you get a silently wrong texture.
- **Severity:** polish
- **Confidence:** high
- **Affects:** asset import on both platforms.
- **Verify by:** a unit test once extension dispatch is a pure helper; or code reading.
- **Fix sketch:** lower-case the extension before comparing.

## Rejected
- **Free of a resource whose upload is queued but not submitted:** `retireValue()` is `lastSubmitted+1`,
  and the open upload batch is always the next submission (`Engine::run` flushes before
  `renderFrame`; `submitImmediate` flushes first). Safe today.
- **Editor "Destroy Selected Entity" inside `UiPass::execute`:** freeing a `StaticMesh` mid-recording
  retires at the frame's own submission, because nothing submits in between. Safe, but fragile (see notes).
- **Image state after upload or IBL:** `ResourceState::ShaderRead` maps to `eAllCommands`, which covers
  compute and fragment readers. `Ibl` transitions every `Texture::create(desc)` image from Unknown
  before use.
- **Assets outliving the Device:** `Engine` declares `m_defaults`, `m_ibl`, `m_brdfLut`, `m_scene` and
  `m_assets` after `m_device`. The legacy layer holds no asset `shared_ptr`s, and leaked scripts never run
  destructors. `aegis-test` uses the same order.
- **`flushUploads` loses a batch when the submit fails:** real, but only on queue-submit failure (device
  lost). Belongs to the rhi lens.
- **`uploadBlocking`'s 16-attempt cap:** each flush submits or reclaims one batch. With 2 outstanding
  batches and threshold < ring size, an upload succeeds within about 3 tries.
- **Partial glTF load leaving null entities:** entities are created only in `buildScene`, after every load
  has succeeded. Created resources are freed by their destructors.
- **8/16/32-bit indices, sparse accessors, accessors without a bufferView, normalized UVs:** fastgltf's
  `iterateAccessor` converts or zero-fills all of these.
- **URIs with spaces or case mismatches:** fastgltf percent-decodes URIs. Every bundled image and buffer
  URI and every `ASSETS_DIR` path in the examples matched the directory listings exactly.
- **Missing tangents:** shaders build the TBN from derivatives (`shaders/modules/tbn.slang`).
- **Focus loss and stuck keys:** GLFW releases keys and buttons on focus loss, and the controller
  re-reads the cursor when rotation is enabled.
- **Exact-modifier match in `Input::onKey`:** lock-key mods are off by default, so Ctrl+S matches as intended.
- **Invalid hierarchies (node with two parents) and out-of-range indices:** invalid glTF only.
- **OBJ `.mtl` resolved relative to cwd:** OBJ materials are unused anyway.
- **`int` cast of `encoded.size()` in `loadFromMemory`:** only matters above 2 GB, which is unrealistic.
- **`DefaultAssets` failure handled as `T{}` by `Engine::check`:** already documented in
  engine-architecture.md §2.

## Notes for lens a
- `GltfImport` does parse, decode, upload and ECS construction in one anonymous-namespace class. A pure
  "glTF → model description" step (`MeshPreprocessor::Input` per primitive, material params, node tree)
  would make 02–06, 09 and 15 unit-testable with tiny in-memory glTFs (fastgltf parses from memory).
- A single bad texture fails the whole model. Falling back to defaults with a warning list is a policy
  choice.
- PBR parameter names are string literals in the loader, the template and `DefaultAssets`, and the
  shader struct (`common.slang` `PbrMaterial`) is hand-mirrored in insertion order.
- The texture format per role is decided in the loader. There is no dedupe of images shared by several
  textures, and no asset cache across `Loader::load` calls.
- Deferred-free correctness rests on "no queue submission between a free and the submit of the frame
  that last recorded the resource". That holds with one queue and flush-first ordering. An async or
  transfer-queue upload, or a mid-frame flush, would break it.
- The agent-notes advice "construct `Input` before `ImGui_ImplGlfw_InitForVulkan`" no longer applies.
  `Window` installs its key callback in its constructor, and `Input` only sets `m_keyHandler`, so ImGui
  chains to `Window::onKey` whatever the order.
- `Window`/`Input` are `friend`-coupled, and `platform` has no error channel. `GLFWwindow*` and GLFW macro
  values leak into the public API.
- `Texture::create(desc)` without data leaves the image in `ResourceState::Unknown`. This is documented,
  and it is the only route for compute-filled textures.
