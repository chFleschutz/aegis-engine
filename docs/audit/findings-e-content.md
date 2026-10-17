# Findings, lens e (testability): content and platform

Scope: `src/aegis/assets`, `src/aegis/platform`, their tests (`tests/assets`, `tests/CMakeLists.txt`) and consumers (`src/aegis/test/main.cpp` `verifyInput`/`verifyAssets`, `src/aegis/engine.cppm`, examples). Read-only pass; nothing was built or run. "Observed" = what the code says; "Inference" = reasoning not checked by running. Map facts were re-checked; corrections noted inline.

## 1. What is tested today

`aegis-tests-assets` links only `aegis-assets` (which brings `aegis-scene`, `aegis-core`, `aegis-rhi` PUBLIC). Two files, 11 `TEST_CASE`s (map/content.md says "4 cases in 3 suites" for material; it is 5).

| Unit | File | Cases | Depth (honest) |
|---|---|---|---|
| `MeshPreprocessor::process` | `tests/assets/mesh_preprocessor.test.cpp` | 6 (one with 4 SUBCASEs) | Good for its size: dedupe of an unindexed quad, meshlet triangle coverage, bounding sphere contains vertices, default uv/color, indexed input kept, all four `validate()` rejections. Not covered: meshlet limits respected / more than one meshlet, index remap preserves geometry (only counts checked), `meshletVertices`/`meshletPrimitives` ranges, uv/color carried through dedupe (only defaults checked). |
| `MaterialTemplate` layout | `tests/assets/material.test.cpp` | 2 | Offsets of 5 scalar/vector types incl. vec3-after-float; unknown-name lookup. |
| `detail::scalarSize/scalarAlignment` | same | 2 | All value kinds incl. texture = 8 bytes. |
| `detail::writeParameters` | same | 1 | Defaults then one float override. Not covered: null texture written as `0xFFFFFFFF` pair (documented contract), int/uint values. |

No unit test for: `Loader` (glTF/OBJ), `GltfImport`, `Texture` (decode, format/size checks), `StaticMesh::GpuData`, `DefaultAssets`, `MaterialInstance`, `detail::uploadBlocking`, `Error`, and all of `platform` (no `tests/platform/`, contrary to `tests/README.md` line 3).

Device level (label `device`, skipped by CI): `verifyAssets` (`test/main.cpp:579`) creates `DefaultAssets`, reads back the white texture, checks per-frame material blocks differ, loads `DamagedHelmet.gltf` + `Misc/cube.obj` into a real `scene::Registry`, expects 2 meshes / 1 albedo 2048x2048 with 12 mips. `verifyInput` (`test/main.cpp:519`, windowed, Win32 only) checks `keyPressed`, `mouseButtonPressed`, and one `bind` case (press fires once; a Control-modified binding does not). Both are single happy-path smoke checks; no error paths.

## 2. Pure logic worth testing that is not tested

| Unit (file + symbol) | Why it matters |
|---|---|
| glTF accessor reading, `GltfImport::loadMeshes` (`gltf_loader.cpp:64`) | Every new vertex attribute (tangents, TEXCOORD_1, skins) lands here. Required-attribute errors, optional attributes, index widths and primitive->submesh mapping are untested. |
| glTF material mapping, `GltfImport::loadMaterials` (`:169`) | Hard-coded name strings (E-CONTENT-02); every glTF material feature is added here. |
| sRGB/linear choice, `GltfImport::loadTextures` (`:124-132`) | Wrong color space is a silent visual bug (E-CONTENT-07). |
| Node hierarchy, `GltfImport::buildScene` (`:205`) | Parenting, multi-primitive children, Y-up->Z-up root, which nodes are instantiated (E-CONTENT-06). |
| OBJ stream building, `Loader::loadObj` (`obj_loader.cpp:19`) | Missing normals rejected, uv/color defaults, UV convention (E-CONTENT-05). |
| Format dispatch, `Loader::load` (`loader.cpp:16`) | Case-sensitive extension match (E-CONTENT-12). |
| Image decode, `Texture::loadFromFile/loadFromMemory/loadEquirectangular` (`texture.cpp`) | Format checks, `.hdr` rejection, decode errors all happen before the device call but are reachable only through it (E-CONTENT-08). |
| mip0 size vs desc, `Texture::create(device, desc, mip0)` | No check in assets; RHI only asserts (E-CONTENT-09). |
| `Vertex`, `Meshlet`, `StaticMesh::GpuData` layout (`mesh_data.cppm`, `static_mesh.cppm`) | Hand-mirrored in Slang; `sizeof`/`offsetof` pins need no seam. |
| Material validation (`material.cpp`) | Assert-only; release writes wrong bytes (E-CONTENT-03). |
| `MaterialInstance` dirty tracking | 5 lines behind `create(Device&)`; low value alone (section 4). |
| `DefaultAssets` PBR template vs `shaders/modules/common.slang` `PbrMaterial` ("in order", line 100) | Not unit-testable as is: texture defaults need a device (E-CONTENT-02). |
| `Input::onKey` binding match (`input.cpp:62`) | Pure, reachable only through a live GLFW window (E-CONTENT-10, -11). |
| Cursor delta | Not in platform: `KinematicMovementController` keeps `m_previousCursorPos` itself (`scripting/movement/kinematic_movement_controller.cppm:49,91,155`). One consumer; not worth a seam yet. |
| `Window` resize bookkeeping | Three assignments (`window.cpp:76-97`); covered by the windowed run; not worth a seam. |

## 3. Blockers per unit

| Unit | Blocker |
|---|---|
| glTF import | One anonymous-namespace class (`GltfImport`) interleaves parse, accessor reads, `MeshPreprocessor`, `StaticMesh::create` (Device), texture decode+upload (Device), `MaterialInstance::create` (Device) and ECS writes. The first Device call happens in the first primitive, so nothing after parsing runs without a GPU (E-CONTENT-01). |
| glTF import | fastgltf is linked PRIVATE to `aegis-assets`, so a test cannot name `fastgltf::Asset` even if the class were exported. Any seam must take a path/bytes and return engine-owned plain types. |
| OBJ import | Same shape in one function: tinyobj parse, streams, `StaticMesh::create(desc.device)`, `desc.scene.create`. |
| Loader + ECS | Correction to the map's framing: `scene::Registry` is default-constructible (`scene/registry.cppm:20`) and already linked into `aegis-tests-assets`, so it is not a test blocker; `rhi::Device&` is. The Registry is a layering problem (U1), not a testability one. |
| Texture decode | Decode and upload in one function; `Texture` has only Device-backed constructors (E-CONTENT-08). |
| Material validation | `assert` only (`material.cpp:78-79,135-136,148`): aborts in a test, UB in release (E-CONTENT-03). |
| `MaterialInstance` | `create` allocates a `CpuWrite` buffer. Acceptable (section 4). |
| `DefaultAssets` | Texture parameters need non-null defaults, and shaders sample every map unconditionally (`shaders/cpu-driven/geometry.slang:57-63`), so the rule is a real GPU contract. |
| `Input` bindings | `Input(Window&)` calls GLFW in its constructor and installs itself via `friend` access to `Window::m_keyHandler`; `Window` needs `glfwInit` and a display (E-CONTENT-10). |
| `Window` | Constructor asserts; `glfwInit()` unchecked (E-CONTENT-13). |

## 4. Seams (smallest per blocker)

**S1. CPU import -> `ImportedModel`, then instantiation (E-CONTENT-01).**
```cpp
struct ImportedModel                       // plain data, no Device, no Registry
{
    struct Primitive { MeshData mesh; std::optional<std::size_t> material; };
    struct Material  { glm::vec4 baseColor; float metallic, roughness; glm::vec3 emissive;
                       std::optional<std::size_t> baseColorImage, metalRoughnessImage, normalImage,
                                                  occlusionImage, emissiveImage; };
    struct Image     { std::uint32_t width, height; std::vector<std::byte> rgba8; bool srgb; std::string name; };
    struct Node      { std::string name; glm::vec3 t; glm::quat r; glm::vec3 s;
                       std::optional<std::size_t> mesh; std::vector<std::size_t> children; };
    std::vector<std::vector<Primitive>> meshes; std::vector<Material> materials;
    std::vector<Image> images; std::vector<Node> nodes; std::vector<std::size_t> roots;
    std::vector<std::string> warnings;
};
auto importModel(const std::filesystem::path&) -> std::expected<ImportedModel, Error>;
auto importGltf(std::span<const std::byte>, const std::filesystem::path& base) -> std::expected<ImportedModel, Error>; // tests
auto importObj(std::istream&) -> std::expected<ImportedModel, Error>;                                                  // tests
// Loader::load = importModel + format-independent instantiation (GPU objects, then entities)
```
- Replaces: the CPU halves of `GltfImport::{loadMeshes, loadTextures, loadMaterials}` and of `loadObj`. Their Device/Registry halves plus `buildScene` become one small instantiation function.
- Unlocks: all S1 tests below, on in-memory glTF JSON (fastgltf parses from bytes; buffers as base64 `data:` URIs) and OBJ strings, with no GPU. A glTF feature then means "field in `ImportedModel`, mapped in import, used in instantiation", and each half can be tested.
- Not over-engineering: a struct of vectors, like `MeshData` already is; no interface, no virtuals. Put it in a new `:import` partition that imports neither `aegis.rhi` nor `aegis.scene.registry`, so the partition boundary enforces CPU-only (as `:ring_allocator` does in rhi). It is also the unit that caching (by path) and async loading (import on a worker, instantiate on the main thread) would need, and it confines the U1 edge to one function that could later move to the app layer. Size M.
- Implementer decision: decode images during import (simpler, makes decode testable, holds CPU pixels until instantiation) vs keep encoded bytes. Recommend decode in import.

**S2. Material validation returns errors (E-CONTENT-03).** `MaterialTemplate::check(name, const MaterialValue&) const -> std::expected<void, Error>` (unknown name, type mismatch, null texture); `addParameter`/`setParameter` return `[[nodiscard]] std::expected<void, Error>` and delegate; `MaterialInstance::parameter` returns a pointer (or expected) instead of asserting. Replaces the 5 asserts. Tests run on a bare `MaterialTemplate`. Size S plus call-site churn (4 in `gltf_loader.cpp`, 1 in `defaults.cpp`, examples). Do not extract dirty tracking into its own type (5 lines, one consumer, `verifyAssets` checks per-frame blocks).

**S3. `assets::detail::decodeImage(span<const byte>)` / `decodeImageFile(path)` -> `expected<DecodedImage{width,height,rgba8}, Error>` (E-CONTENT-08).** `loadFromFile/loadFromMemory` become decode + `create`. S1 needs this function anyway, so it is a step of S1. A 2x1 PNG fits in a byte array in the test. Size S.

**S4. mip0 size check via the existing RHI helper (E-CONTENT-09).** `assets::detail::expectedMip0Size(const Texture::Desc&)` built on `rhi::detail::subresourceFootprints(extent, format, 1, arrayLayers)` (already exported, `rhi/image.cppm:116`); `Texture::create(desc, mip0)` returns `InvalidData` on mismatch before creating anything. Size S.

**S5. `platform::KeyBindings` value type (E-CONTENT-10, -11).** Move `Input::Binding`, `m_bindings`, `bind`, `onKey` into a plain class: `bind(Key, callback, KeyAction, Modifiers) -> BindingId`, `unbind(BindingId)`, `dispatch(Key, KeyAction, Modifiers)`. `Input` holds one and forwards from `Window::m_keyHandler`. Replaces ~20 lines of `input.cpp`. Runs on any CI runner (GLFW headers only; `glfw` is already PUBLIC and CI installs its link deps). This is the `RingAllocator` pattern from `tests/README.md` applied to platform. Size S.

**S6. `Window::create(Desc) -> std::expected<Window, Error>` (E-CONTENT-13).** Replaces the asserting constructor and unchecked `glfwInit`. Does not make `Window` unit-testable (it needs a display), but lets `aegis-test` and future example CTests report "no display" instead of aborting. Size S.

Rejected as over-engineering: a fake `rhi::Device` for assets (after S1-S4 only creation glue remains, and `verifyAssets` already runs it); `IWindow`/`IInput` interfaces (one backend, and S5 covers the only logic); a separate `MaterialOverrides` type.

## 5. Findings

### E-CONTENT-01: Model import has no CPU intermediate; parse, decode, upload and ECS writes are one pass
- **Where:** `src/aegis/assets/gltf_loader.cpp` `GltfImport` (anonymous namespace), `obj_loader.cpp` `Loader::loadObj`
- **Observed:** `GltfImport::run` calls `loadMeshes()` (accessor reads -> `MeshPreprocessor::process` -> `StaticMesh::create(m_desc.device, ...)` per primitive), `loadTextures()` (`Texture::loadFromFile/loadFromMemory(m_desc.device, ...)`), `loadMaterials()` (`MaterialInstance::create(m_desc.device, ...)`), then `buildScene()` (`m_desc.scene.create/add/addChild`). `loadObj` does tinyobj -> streams -> `StaticMesh::create(desc.device)` -> `desc.scene.create`. fastgltf is a PRIVATE dependency.
- **Inference:** No glTF/OBJ logic past parsing can run without a GPU, and since fastgltf is private, exporting the class would not help. Each new glTF feature edits a class that mixes four concerns, and its only check is the device smoke test (one model).
- **Severity:** design problem
- **Confidence:** high
- **Affects:** extensions: new vertex attributes, material features, cameras/lights, asset caching, async loading, cutting assets -> scene (U1). Tests: every loader test.
- **Proposal:** S1 (size M). Unlocks candidate tests 3-8, 13-14.

### E-CONTENT-02: glTF material mapping and the PBR template are coupled by string literals
- **Where:** `gltf_loader.cpp` `GltfImport::loadMaterials` (lines 180-193); `defaults.cpp` `DefaultAssets::create` (lines 43-52); `loader.cppm` `Loader::Desc::pbrTemplate` doc; `shaders/modules/common.slang:100` `PbrMaterial`
- **Observed:** Nine names (`"albedo"`, `"metalRoughnessMap"`, ...) are spelled independently in the loader, `defaults.cpp`, and examples. `Loader::Desc` says "the default PBR template's parameter names are expected". The Slang struct says "(assets::DefaultAssets::pbrTemplate, in order)".
- **Inference:** A typo or rename fails only at runtime: debug asserts, release silently stores an override that `writeParameters` never writes (unknown names are inserted and ignored, `material.cpp:138`). Reordering `addParameter` calls desynchronizes the GPU block with no check. Passing any other template in `Loader::Desc` asserts. A new material model needs coordinated edits in three places.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** extensions: new material models (unlit, clearcoat, KHR_materials_*), template changes. Tests: PBR layout cannot be unit-tested (texture defaults need a Device).
- **Proposal:** (a) Export one table of PBR parameter names as `inline constexpr std::string_view`s in `:defaults`, used by `defaults.cpp`, the loader and the examples. A typo then fails to compile. Size S. (b) With S1, the mapping becomes one function `applyPbr(const ImportedModel::Material&, textures, MaterialInstance&)`. (c) Mirror `PbrMaterial` as a C++ struct and have `DefaultAssets::create` return `InvalidData` if template offsets differ from its `offsetof`s. That is checked by the device tests, which is the honest limit here, since a texture default needs a Device.

### E-CONTENT-03: Material validation is assert-only; release builds write wrong bytes or crash
- **Where:** `material.cpp` `MaterialTemplate::addParameter`, `MaterialInstance::setParameter`, `MaterialInstance::parameter`, `detail::writeParameters`
- **Observed:** `assert(parameter->defaultValue.index() == value.index() && "Material parameter type mismatch");` then `m_overrides.insert_or_assign(name, std::move(value));`. `writeParameters` memcpys `sizeof(T)` of the override's own type at the parameter's offset. `parameter(name)` derefs `m_template->parameter(name)` after an assert.
- **Inference / failure scenarios (release):** (1) `material->setParameter("roughness", 1)`: the int literal selects the `std::int32_t` alternative of `MaterialValue`, so the block gets the bits `0x00000001` and the shader reads roughness of about 1.4e-45 (mirror-smooth). Debug aborts. (2) `setParameter("albedo", glm::vec4{...})` writes 16 bytes at offset 0 and overwrites `emissive.x`. (3) `parameter("albedoMp")` with no override derefs nullptr. Examples call `setParameter` with literals (`eval-scene/main.cpp:203-205,282-284`), so (1) is one typo away.
- **Severity:** bug
- **Confidence:** high
- **Affects:** extensions: any user material code and editor material editing. Tests: validation cannot be tested at all (an assert aborts the test binary).
- **Proposal:** S2 (size S). Unlocks candidate tests 1, 2.

### E-CONTENT-04: The default material is one shared, mutable instance
- **Where:** `obj_loader.cpp` `Loader::loadObj` (line 68); `gltf_loader.cpp` `GltfImport::materialFor`; `engine.cppm:117` (`.defaultMaterial = engine.m_defaults->pbrMaterial`)
- **Observed:** Every OBJ entity and every glTF primitive without a material gets the same `shared_ptr<MaterialInstance>`: `DefaultAssets::pbrMaterial`. `examples/helmets/main.cpp:19-20,62-65` attaches `ColorChanger` to `plane.obj`, which calls `get<assets::Material>().instance->setParameter("albedo", color)` every frame.
- **Inference / failure scenario:** load two OBJs (or one OBJ plus any material-less glTF primitive) and attach `ColorChanger` to one. Both cycle colors, because the script mutates the engine-wide default. In Helmets today only the plane uses it, so the bug is latent there.
- **Severity:** bug
- **Confidence:** high (mechanism), medium (visible today)
- **Affects:** extensions: per-entity material editing (editor), scripts. Tests: none possible today (Device).
- **Proposal:** Instantiation creates one fresh `MaterialInstance` of `pbrTemplate` per loaded model for material-less primitives (OBJ: always), instead of sharing the global. `Loader::Desc::defaultMaterial` goes away. With S1, "no material" is an explicit `nullopt` in `ImportedModel`, testable on the import side. Add a device check in `verifyAssets` ("two loads do not share a material"). Size S.

### E-CONTENT-05: OBJ texture coordinates are not flipped to the engine's top-left convention
- **Where:** `obj_loader.cpp` `Loader::loadObj` (lines 49-52)
- **Observed:** `input.uvs.emplace_back(... attrib.texcoords[2*uv+0], attrib.texcoords[2*uv+1])`, copied unchanged. The glTF loader also copies unchanged, which is correct for glTF (top-left origin). No `stbi_set_flip_vertically_on_load` anywhere in `src/`; the shaders sample `input.uv` unchanged. `plane.obj` has standard 0..1 `vt`. `simple-scene/main.cpp:26,40-42` puts `painting.png` on `plane.obj`.
- **Inference:** OBJ's v axis starts at the bottom, so OBJ-textured surfaces sample vertically mirrored. The painting in Simple-Scene should appear upside down. Not run, and it depends on how the plane is oriented on screen.
- **Severity:** bug
- **Confidence:** medium
- **Affects:** extensions: OBJ materials (MTL) when added. Tests: OBJ UV convention.
- **Proposal:** `v' = 1 - v` when building the OBJ streams. Pin it with candidate test 13 (S1, in-memory OBJ). Confirm with a Simple-Scene screenshot. Size S.

### E-CONTENT-06: glTF import instantiates every node in the file, not the default scene's
- **Where:** `gltf_loader.cpp` `GltfImport::run` / `buildScene`
- **Observed:** `sceneIndex` is used only for the root's name (`run`, lines 52-54). `buildScene` creates an entity for every `m_gltf.nodes[i]` and parents every parentless one to `m_root` (lines 250-254).
- **Inference / failure scenario:** a file with `"scenes":[{"nodes":[0]},{"nodes":[1]}],"scene":0` renders both node 0 and node 1. Orphan nodes that belong to no scene are also instantiated.
- **Severity:** bug
- **Confidence:** high (mechanism), low impact (the in-tree assets have one scene)
- **Affects:** extensions: multi-scene/variant files, glTF cameras/lights nodes. Tests: hierarchy.
- **Proposal:** Walk from `scenes[sceneIndex].nodeIndices` (`ImportedModel::roots` in S1). Candidate test 7. Size S.

### E-CONTENT-07: Texture color space is chosen per texture by role, and conflicts are silent
- **Where:** `gltf_loader.cpp` `GltfImport::loadTextures` (lines 124-132)
- **Observed:** All textures default to `RGBA8_UNORM`; any texture used as baseColor or emissive by any material becomes `RGBA8_SRGB`.
- **Inference:** A texture used as baseColor in one material and as normal/metal-roughness in another is decoded as sRGB for both, with no notice. The rule is correct for the common case; the gap is that it is untested and that adding roles (e.g. KHR_materials_specular color textures) edits a block buried in an upload loop.
- **Severity:** polish
- **Confidence:** high
- **Affects:** extensions: new texture roles. Tests: color-space selection.
- **Proposal:** Pure `detail::imageColorSpaces(materials, imageCount)` inside S1, with a warning on conflict. Candidate test 6. Size S.

### E-CONTENT-08: Image decode is reachable only through GPU texture creation
- **Where:** `texture.cpp` `Texture::loadFromFile`, `loadFromMemory`, `loadEquirectangular`, anonymous `createFromRgba8`
- **Observed:** Each function checks extension/format, calls `stbi_load*`, then immediately `Texture::create(device, ...)`. The decoded pixels are a local `StbiPixels`.
- **Inference:** The `.hdr` rejection, the RGBA8-only format check and decode failures (all CPU) can only be exercised with a Device, and the glTF importer cannot decode without uploading.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** extensions: KTX2/compressed formats, async decode, S1. Tests: decode errors and sizes.
- **Proposal:** S3 (size S). Candidate tests 15, 16.

### E-CONTENT-09: `Texture::create(desc, mip0)` does not validate the data size
- **Where:** `texture.cpp` `Texture::create(rhi::Device&, const Desc&, std::span<const std::byte>)`; `rhi/upload_manager.cpp:156`
- **Observed:** No size check in assets. The RHI has `assert(data.size() == last.sourceOffset + last.size && "Upload data does not match image layout");` and then copies mip by mip from the caller's span.
- **Inference / failure scenario (release):** `Texture::create(device, {.extent={2,2,1}, .format=RGBA32_SFLOAT, .mipLevels=1}, 16 bytes)` reads 64 bytes from a 16-byte span (out-of-bounds read). Debug aborts. This is public API (Ibl and users), and the module promises `std::expected`.
- **Severity:** bug
- **Confidence:** medium (the OOB copy is inferred from the comment and the assert, not traced line by line)
- **Affects:** extensions: procedural textures, KTX2 upload. Tests: size math.
- **Proposal:** S4 (size S). Candidate test 17.

### E-CONTENT-10: Key-binding dispatch is reachable only through a live GLFW window
- **Where:** `platform/input.cpp` `Input::Input`, `Input::bind`, `Input::onKey`; `window.cppm` `friend class Input`, `m_keyHandler`
- **Observed:** The `Input` constructor calls `glfwRawMouseMotionSupported`/`glfwSetInputMode` and writes `m_window.m_keyHandler`. `onKey` is private. The only check is `verifyInput`, which is windowed and Win32-only, and whose comment (line 517) admits "Assumes no modifier key is physically held".
- **Inference:** The matching rules (action filter, exact modifiers, multiple bindings) cannot be tested on CI or Linux, and every input feature (unbind, chords, repeat handling) has no test home.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** extensions: input actions/rebinding, editor shortcuts, scripts. Tests: all binding tests.
- **Proposal:** S5 (size S). Candidate tests 9-12.

### E-CONTENT-11: Key bindings cannot be removed, and binding from a callback is undefined behaviour
- **Where:** `input.cpp` `Input::bind`, `Input::onKey`
- **Observed:** `bind` does `m_bindings[key].emplace_back(...)`. `onKey` range-for iterates `bindings->second` by reference and calls each callback. There is no `unbind`. `verifyInput` uses a `shared_ptr` counter because "the bindings outlive this call" (line 535).
- **Inference / failure scenarios:** (1) a callback on key K that calls `input.bind(K, ...)` (a toggle that re-arms itself) can reallocate the vector during iteration, which is UB. (2) A script binding a lambda that captures `this` leaves a dangling callback once the script dies. Today scripts are never deleted (engine-architecture §5), so this goes live when the planned World/`reset()` lands.
- **Severity:** bug (latent)
- **Confidence:** medium (no in-tree caller triggers it today; only `verifyInput` and unbuilt `ai/` call `bind`)
- **Affects:** extensions: scene switching, script lifetimes. Tests: candidate tests 11, 12.
- **Proposal:** In S5: `bind` returns an id, add `unbind(id)`, and `dispatch` iterates by index over a size snapshot. Size S.

### E-CONTENT-12: Model format dispatch is case-sensitive
- **Where:** `loader.cpp` `Loader::load` (lines 16-20); `texture.cpp` `Texture::loadFromFile` (line 124)
- **Observed:** `extension == ".gltf" || extension == ".glb"`, `== ".obj"`; `path.extension() == ".hdr"`.
- **Inference / failure scenario:** `Model.GLB` or `CUBE.OBJ` returns `UnsupportedFormat`. `Sky.HDR` passes the HDR guard and stb decodes it to 8-bit LDR without error.
- **Severity:** polish
- **Confidence:** high
- **Affects:** extensions: new formats (dispatch table). Tests: dispatch.
- **Proposal:** `detail::modelFormat(const std::filesystem::path&) -> std::optional<ModelFormat>` comparing lowercase extensions; reuse it for the `.hdr` guard. Candidate test 21. Size S.

### E-CONTENT-13: `Window` has no error channel and terminates GLFW per window
- **Where:** `platform/window.cpp` `Window::Window`, `Window::~Window`
- **Observed:** `glfwInit();` (result unchecked); `assert(m_window && "Platform Error: Failed to create window");`; the destructor calls `glfwDestroyWindow` then `glfwTerminate()`. AGENTS.md lists `platform/` as new style ("std::expected ... never throw").
- **Inference / failure scenarios:** on a machine without a display (Linux CI, SSH), debug aborts; release continues with a null window into `glfwSetWindowUserPointer`. With two `Window`s, destroying one terminates GLFW under the other.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** extensions: headless examples as CTests (engine-architecture "AppDesc"), multi-window/editor viewports. Tests: lets windowed checks skip cleanly instead of crashing.
- **Proposal:** S6, plus GLFW init/terminate owned by one platform-level object (or a reference count) rather than by each window. Size S.

### E-CONTENT-14: Import errors are all-or-nothing and unsupported glTF data is dropped silently
- **Where:** `gltf_loader.cpp` `GltfImport::loadTextures`, `loadMaterials`; `assets/error.cppm` `Error`
- **Observed:** The first failing texture (missing file, undecodable image) returns an error for the whole model. Ignored without notice: baseColor alpha (`glm::make_vec3(baseColorFactor.data())`, line 180), `alphaMode`, `textureInfo.texCoord`, samplers, `KHR_texture_transform`, normal scale, occlusion strength, cameras, lights. The result type has no warning channel.
- **Inference:** Each glTF feature added forces a fail-or-degrade policy decision inside `GltfImport`, and users get no signal for what was skipped.
- **Severity:** design problem
- **Confidence:** medium
- **Affects:** extensions: every glTF feature. Tests: "degrades with a warning" cases.
- **Proposal:** `ImportedModel::warnings` (S1). A missing or undecodable texture falls back to the default texture with a warning; required data (POSITION/NORMAL) stays an error. Candidate test: "a missing texture file yields a warning, not an error". Size S on top of S1.

## 6. Candidate tests, ranked

| # | TEST_SUITE / TEST_CASE | Unit | Seam | Value | Effort |
|---|---|---|---|---|---|
| 1 | `assets::MaterialTemplate` / "rejects a value whose type differs from the parameter's default" | `MaterialTemplate::check` | S2 | high (release UB) | S |
| 2 | `assets::MaterialTemplate` / "rejects an unknown parameter name" | same | S2 | high | S |
| 3 | `assets::importGltf` / "reads positions, normals, uvs and indices of each primitive" | gltf import | S1 | high | M (fixture helper) |
| 4 | `assets::importGltf` / "fails with InvalidData when a primitive lacks NORMAL" | gltf import | S1 | high | S |
| 5 | `assets::importGltf` / "maps PBR factors and texture indices of each material" | material mapping | S1 | high | S |
| 6 | `assets::importGltf` / "marks baseColor and emissive images sRGB and the rest linear" | color space | S1 | medium | S |
| 7 | `assets::importGltf` / "imports only the default scene's nodes, with their parent links" | hierarchy | S1 | medium | S |
| 8 | `assets::importGltf` / "gives a multi-primitive mesh one submesh per primitive" | mesh mapping | S1 | medium | S |
| 9 | `platform::KeyBindings` / "fires only for the bound action and exact modifiers" | `KeyBindings::dispatch` | S5 | high (only Win32-windowed today) | S |
| 10 | `platform::KeyBindings` / "calls every binding of a key in bind order" | same | S5 | medium | S |
| 11 | `platform::KeyBindings` / "survives a callback that binds the same key" | same | S5 | medium (UB today) | S |
| 12 | `platform::KeyBindings` / "stops calling a callback once unbound" | same | S5 | medium | S |
| 13 | `assets::importObj` / "flips v to a top-left origin" | OBJ streams | S1 | medium | S |
| 14 | `assets::importObj` / "rejects a face corner without a normal" | OBJ streams | S1 | low | S |
| 15 | `assets::detail::decodeImage` / "decodes a 2x1 PNG into 8 RGBA bytes" | decode | S3 | medium | S |
| 16 | `assets::detail::decodeImage` / "reports DecodeFailed for bytes that are not an image" | decode | S3 | medium | S |
| 17 | `assets::detail::expectedMip0Size` / "covers every array layer of mip 0" | size math | S4 | medium | S |
| 18 | `assets::detail::writeParameters` / "writes a null texture as two invalid indices" | existing | none | low | S |
| 19 | `assets::Vertex` / "matches the shader's vertex layout byte for byte" (plus `Meshlet`, `StaticMesh::GpuData` via `sizeof`/`offsetof`) | layout pins | none | medium (CPU/GPU drift) | S |
| 20 | `assets::MeshPreprocessor` / "keeps every meshlet within the vertex and triangle limits" (input larger than one meshlet) | existing | none | medium | S |
| 21 | `assets::detail::modelFormat` / "recognizes model extensions regardless of case" | dispatch | E-CONTENT-12 helper | low | S |

## 7. Strategy for this group

- **Fixtures.** CI checks out all submodules (`.github/workflows/build.yml` lines 30-32 and 92-94, `submodules: recursive`), so `modules/aegis-assets` is available on CI, pinned by the submodule commit. It is still the wrong default for unit tests: the real models lack the edge cases (missing NORMAL, two scenes, conflicting texture roles, embedded images), and they are large and change with submodule bumps. Recommended: hand-written fixtures inside the test sources. glTF as a raw-string JSON with base64 `data:` buffers, parsed from bytes through `importGltf(span, base)`; OBJ as a string through `importObj(std::istream&)` (tinyobjloader parses from streams); PNG as a byte array (a 2x1 PNG is about 70 bytes). This needs no files and no path baking. Optionally add one smoke test over `Misc/cube.obj` using an `ASSETS_DIR` compile definition (the pattern `src/aegis/test/CMakeLists.txt:33` already uses), with an extra CTest label (e.g. `assets-data`) so a checkout without submodules can exclude it.
- **Platform without a window.** Only `KeyBindings` (S5) needs unit tests. Add `tests/platform/` with `aegis_add_test(platform SOURCES key_bindings.test.cpp LIBS aegis-platform)`. GLFW headers are fine in a test TU, and CI already installs GLFW's Linux link dependencies. `Window` stays covered by the windowed `aegis-test` run. Cursor delta and resize bookkeeping do not earn a seam yet.
- **Fake device: not needed.** Everything worth unit testing here is extractable (S1-S5). What stays Device-bound is creation glue (`StaticMesh::create`, `Texture::create`, `MaterialInstance::create`, instantiation), which `verifyAssets` already exercises. Extend `verifyAssets` with two cheap checks instead of mocking: "two loads do not share a material" (E-CONTENT-04) and "a wrong-sized mip0 returns InvalidData" (E-CONTENT-09).
- **Test policy (challenge).** `tests/README.md`'s "extract, don't mock" holds up well for this group. Two refinements: (a) extracted CPU stages should get their own partition with no `aegis.rhi` import, so the boundary is enforced, as `:ring_allocator` is; (b) the policy should allow small in-source data fixtures (JSON, OBJ, PNG bytes) for importer tests, which the README does not mention. The README's scope line naming `platform` as covered, and its layout block, are stale (map M8/M9).
- **Linking.** `aegis-tests-assets` builds against legacy `aegis-scene`/`aegis-core` because of U1 and `core::Asset`. This does not block any test above. Once S1 makes instantiation a single function, moving it out of `aegis.assets` is what would cut U1.
