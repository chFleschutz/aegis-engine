# Asset system, scenes and worlds

Status: **decided** (D8, 2026-10-19, settles F2 in `audit/backlog.md`), not implemented. It fills in
the boundaries of [`editor-runtime-split.md`](editor-runtime-split.md): assets referenced only by
stable ID, one serializer for scenes and save games, a cook step for export.

**Today** loaders decode and upload in one step and create entities directly, nothing is cached
(shared glTF images load twice, A-CONTENT-04), an asset's identity is its path (`core::Asset`), and
fastgltf, stb_image and meshoptimizer run on every start.

**The model:** Godot's and Unity's sidecars and import cache, Bevy's typed references, O3DE's
`{source, sub}` IDs. Not Unreal's committed binary packages: imported data depends on the importer
version, its settings and the platform, and Aegis pins vertex and meshlet layouts to the shaders, so
committed imports would need a migration on every layout change, live in git next to their source,
and need reimporting by hand.

## Terms

- **World:** the live runtime object (registry, systems, scripts, clock). Never a file; several can
  exist (edit, play, preview).
- **Scene:** an asset, entities serialized in a `.scene` file. Never live.
- **Prefab:** not a type; a scene used as a template.
- **Instance:** the entities a scene produced in a world; its root carries the scene's `AssetRef`.
- **Level:** not an engine type; the scene a game instantiates as a world's root.
- **Save game:** a world snapshot through the same serializer; user data, not an asset.

Verbs: *load* a file into a `Scene`, *instantiate* a `Scene` into a `World`, *save* a world or
subtree to a `.scene`, *snapshot* and *restore* save games. Avoid "map"; "layer" stays the
app-level `LayerStack`, so render and collision groups are channels or masks.

## Project layout

```
MyGame/
├─ MyGame.aegis                   project file: name, startup scene, settings
├─ assets/Helmet/
│  ├─ Helmet.gltf                 source, as exported
│  ├─ Helmet.gltf.meta            sidecar: ID, import settings, sub-asset table
│  └─ Helmet.material             native asset: carries its own ID
├─ scenes/Level1.scene
└─ .aegis/cache/                  imported data; git-ignored, safe to delete
```

Sources go in as they are; the editor imports them and reimports on any change. Native files
(scenes, materials) are the asset and are edited in place.

## Identity

`AssetId = { std::uint64_t id; std::uint32_t subId; }`, written as 16 hex digits (subId 0) or
`<id>:<subId>`.

- **64 random bits.** IDs are made on any machine without coordination, so they are random; at
  100,000 assets a collision has odds of about 3 × 10⁻¹⁰ (Godot uses 63 bits). 128 bits buys nothing
  at real counts and costs a wider key: 64 bits compare in one instruction, hash as they are, and keep
  `AssetId` at 16 bytes. A collision can't go unnoticed anyway: the scan and the cook reject
  duplicates. Generated with a `std::mt19937_64` seeded once from `std::random_device`.
- **The ID lives in the file when Aegis owns the format, in a sidecar when it doesn't.** One native
  file is one asset, so moving or committing it can't lose its ID (Unity's classic missing-`.meta`
  bug), and a native sidecar would hold only the ID. A `.slang` or `.lua` asset still gets a sidecar.
- **Foreign files get a sidecar**, `<file>.<ext>.meta` (full name plus `.meta`, so `Helmet.gltf` and
  `Helmet.png` don't collide). Tools that rewrite the source can't wipe the ID, and import settings
  have a home. The sidecar picks the importer; the extension only gives the default.
- **Sub-IDs are assigned, not computed.** The importer keys each part by type and name (index when
  unnamed), gives new parts the next free number and records the table; numbers are never reused.
  Reordered meshes keep their references, and a renamed part is remapped by editing one entry.
- **Built-in assets** that only code creates get reserved IDs, `builtin:<name>`.
- **Duplicates are rejected:** a copied file brings its ID along, and the scan gives the copy a new
  one; the editor's "Duplicate" writes a new ID directly.

**Finding an asset by ID** goes through an index, the one place paths exist:
- *Development:* the database scans `assets/`, reading sidecars and native headers into
  `ID → path (+ sub-asset table)`. It keeps the index in `.aegis/cache/index` with each file's
  modification time and size, so a later start rereads only changed files; the editor's file watcher
  updates it on moves and renames, which leave every reference intact.
- *Shipping:* the cook writes a sorted `{id, type, offset, size}` table into the pak; no scan, no
  paths. Godot (`uid_cache.bin`), Unity (`Library/`) and Unreal (`AssetRegistry.bin`) do the same.
- The index (where every asset is) is separate from the library's cache (assets now loaded). An
  unknown ID is an error naming the ID and the reference's path hint, if any.

```json
{
  "id": "a3f09c2e4b1d7788",
  "importer": "gltf",
  "importerVersion": 1,
  "settings": { "generateNormals": true },
  "subAssets": { "StaticMesh/Helmet/0": 1, "Material/HelmetMat": 2, "Texture/Albedo": 3 }
}
```

## Native files

One text format (the scene format, chosen in 6.10) with a common header:

```json
{ "type": "Material", "id": "5b0e17d2c94af3a1", "version": 1, "template": "builtin:pbr",
  "parameters": { "roughness": 0.4, "albedoMap": "a3f09c2e4b1d7788:3" } }
```

- **The header decides, the extension is for people:** the loader dispatches on `type` through the
  type registry; extensions are plain words (`.scene`, `.material`) for the browser and git.
- **`version` is a schema version.** Native files are committed, so a format change is a migration:
  the loader upgrades old versions, the editor writes the current one.
- **References are `AssetId`s.** A path may sit next to one as a hint for diffs; nothing resolves
  through it.

## Asset types

The C++ type name is the asset type: the registry name, the header's `type` and the sub-asset key
prefix, explicit and stable. `…Data` names CPU import data (`ImageData`, `MeshData`, `ModelData`);
the plain name is the runtime asset an `AssetRef<T>` points to. Components are named for their role,
not after the asset they hold.

| Type | From | Notes |
|---|---|---|
| `Texture` | `.png` `.jpg` `.hdr`, later `.ktx2` | 2D and cube; sidecar holds sRGB, mips, sampler |
| `StaticMesh` | sub-asset of `.gltf` `.glb` `.obj` | vertices, indices, meshlets; `SkinnedMesh` later |
| `Material` | `.material`, or a glTF sub-asset | parameter values for a template (today's `MaterialInstance`) |
| `MaterialTemplate` | built-in (`builtin:pbr`) | parameter list and layout; no file until custom shaders |
| `Environment` | `.hdr` with the environment importer (the `.hdr` default) | sky cube, irradiance, prefiltered maps |
| `Scene` | `.scene`, or a glTF/OBJ import (its sub-asset 0) | entities; also prefabs and models |

- A glTF needs no runtime `Model` type: it imports as a `Scene` with mesh, material and texture
  sub-assets. `ModelData` exists only inside the importer.
- The BRDF LUT doesn't depend on the environment: it becomes `builtin:brdf_lut` (or renderer-owned),
  not part of `Environment`.
- Environment baking needs the GPU (`render_graph::Ibl`). It stays at load time for now and can move
  into the editor's import later; the type doesn't change.

**Not assets:** the project file `<Name>.aegis`, save games and editor settings (user data), engine
shaders (built from `shaders/`), C++ scripts (attached by registered name).

**Later, with their features:** `Shader` (`.slang`; templates then come from shader reflection),
`Script` (a VM language, with a normal sidecar), `AudioClip` (`.wav` `.ogg`), `Font` (`.ttf`, to an
atlas), `Skeleton` and `AnimationClip` (glTF sub-assets), `InputMap` (`.input`, from `KeyBindings`).

## Scenes are prefabs

There is no prefab type (Godot's model). An imported glTF is a scene too, so levels, prefabs and
models share one call:

```cpp
instantiate(world, scene, parent = none)
```

A level is instantiated into an empty world; switching levels clears the world (or makes a new one).
The project file names the startup scene; which scenes are levels is down to folders or tags.

1. **Entity IDs per instance.** Scene files store **local IDs**, unique per file. A world entity's
   ID is `hash(instanceId, localId)`, chained for nested instances. The instance ID is the local ID
   of the entity holding the instance (0 for a world's root scene, a fresh random ID for an instance a
   script spawns, stored on its root so save games reproduce it).
2. **Nothing world-level outside entities.** No scene settings section: sky, gravity and the active
   camera are components. For singletons the first wins and duplicates warn.
3. **Overrides (later):** `(localId, component, field) = value` entries next to the child's
   `AssetRef`, applied after the child. The first version has none.
4. **Unpack** copies an instance's entities into the parent scene and drops the link.
5. **Cycles** fail at load with the chain in the error; the editor refuses to create them.

## Code structure

- **Import** (CPU, no rhi, no scene): source bytes → `ImageData`, `ModelData`. Tested with doctest.
- **Database** (CPU, development builds): scans the project, owns sidecars and the import cache,
  maps `AssetId` → source and sub-asset.
- **Library** (`AssetLibrary`, runtime): `load<T>(AssetId)` → `AssetRef<T>`, deduplicated by ID,
  creating GPU objects from imported data. Its source is the project (importing on a miss) or, in a
  shipped game, the pak.

**`AssetRef<T>`**, not `AssetHandle` (`…Handle` means a pooled rhi resource): the ID plus a shared
slot `{state, T}`, pending, ready or failed. The serializer writes only the ID, and the cook step finds
dependencies through `AssetRef` fields in the registry. `load` is async-shaped (passes fall back to
the defaults while pending), implemented synchronously first; the slot lets hot reload swap contents.

## Import cache and cooking

- **Cache:** `<project>/.aegis/cache/<id>/<subId>.<type>`, git-ignored, keyed by a hash of source
  bytes, settings, importer version and platform, so a changed importer invalidates its own entries.
  It holds the cooked format: one format for development and shipping.
- **Reimport:** the editor watches the project; a start without the editor checks the keys.
- **Cook** (`<Name>-Editor --cook <out>`): follow `AssetRef`s from the project's scenes (plus an
  "always include" list), import what the cache lacks, compile native files to binary, pack with an
  `AssetId → offset` table. Importers then leave the shipped executable.

## Changes and order

Renames:
- `scene::Scene` → `World`, and `aegis.scene` → `aegis.world`, mechanical, with 6.2 (#80). `Scene`
  becomes the serializer's asset type (6.10, #131).
- The `Mesh` and `Material` components merge into
  `MeshRenderer { AssetRef<StaticMesh> mesh; AssetRef<Material> material; }`. Every pass views both
  anyway, and D2's signals then watch one component.
- `MaterialInstance` → `Material`.
- The `Environment` component → `Sky { AssetRef<Environment> environment; }`.
- `Engine::loadModel` / `loadTexture` / `loadEnvironment` → `AssetLibrary` calls by ID;
  `core::Asset` goes (#75).

Order:
1. #72 → #73 (defines the sub-asset keys) → #74 → #75.
2. 6.9 (#130): sidecars, database, `AssetId`, `AssetRef`, the library cache (closes A-CONTENT-04).
3. The import cache (new item).
4. 6.10 (#131): scene files with local IDs and instances.
5. Export (#134): cook and pak.
6. Later: material files and "extract material", overrides, hot reload.

## Open

- Text format for scenes, materials and sidecars (JSON, YAML or our own), chosen with 6.10.
- Cooked blob layout (versioning, compression, alignment) and per-platform texture compression.
- `.blend` import through headless Blender, which every importing machine would need.
- Override details; a `.prefab` extension for the browser (same format either way).
- Hot reload and streaming.
