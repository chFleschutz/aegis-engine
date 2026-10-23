# Asset system, scenes and worlds

Status: **decided** (D8, 2026-10-19; formats 2026-10-20; sample project 2026-10-21; project layout
and external images 2026-10-22; derived sub-asset IDs, settings per part and the cache by key
2026-10-23; settles F2 in `audit/backlog.md`).
Implemented so far (#130, #137, #237, #238): IDs, sidecars, the database, engine content, `AssetRef`,
`AssetLibrary`, the import cache for models (by key) and external images as their own assets; derived
sub-asset IDs (#236), settings per part (#244), scene files and the pak are not. It fills in the boundaries of [`editor-runtime-split.md`](editor-runtime-split.md): assets
referenced only by stable ID, one serializer for scenes and save games, a cook step for export.

**Today** loaders decode and upload in one step and create entities directly, nothing is cached
(shared glTF images load twice, A-CONTENT-04), an asset's identity is its path (`core::Asset`), and
fastgltf, stb_image and meshoptimizer run on every start.

**The model:** Godot's and Unity's sidecars and import cache, Bevy's typed references, one random ID
per asset and per part of a file. Not Unreal's committed binary packages: imported data depends on the
importer version, its settings and the platform, and Aegis pins vertex and meshlet layouts to the
shaders, so committed imports would need a migration on every layout change, live in git next to
their source, and need reimporting by hand.

**What is fixed and what is not.** Everything written into committed files or save games (IDs, keys,
headers, value encoding, paths) is specified here and pinned by golden-file tests, because changing
it breaks existing projects. Everything the cache or the pak holds is regenerated and stays TBD.

## Terms

- **World:** the live runtime object (registry, systems, scripts, clock). Never a file; several can
  exist (edit, play, preview).
- **Scene:** an asset, entities serialized in a `.scene` file. Never live.
- **Prefab:** not a type; a scene used as a template.
- **Instance:** the entities a scene produced in a world, below the entity holding the instance.
- **Level:** not an engine type; the scene a game instantiates as a world's root.
- **Save game:** a world snapshot through the same serializer; user data, not an asset.

Verbs: *load* a file into a `Scene`, *instantiate* a `Scene` into a `World`, *save* a world or
subtree to a `.scene`, *snapshot* and *restore* save games. Avoid "map"; "layer" stays the
app-level `LayerStack`, so render and collision groups are channels or masks.

## Project layout

Decided 2026-10-22. The layout is user-facing and every project depends on it, so the names below
are fixed; anything not listed belongs to the user.

```
my_game/
├─ project.aegis                  project file: name, startup scene, settings, last engine version
├─ CMakeLists.txt                 a few lines: the engine, then aegis_add_game
├─ CMakePresets.json              from the template
├─ content/                       the only scanned folder: everything with an ID
│  ├─ models/helmet/
│  │  ├─ Helmet.gltf              source, as exported (its name is kept)
│  │  ├─ Helmet.gltf.meta         sidecar: ID, import settings, sub-asset table
│  │  └─ helmet.material          native asset: carries its own ID
│  └─ scenes/level_1.scene
├─ src/                           the C++ game module; optional
├─ config/                        reserved: committed settings that outgrow project.aegis
├─ plugins/                       reserved: packages with their own content/ and src/
├─ engine/                        recommended path of the engine as a submodule
├─ .aegis/                        local, never committed
│  ├─ cache/                      import cache and index; safe to delete at any time
│  ├─ editor/                     per-user editor state (imgui.ini, cameras; see editor.md)
│  └─ saved/                      what development runs write: save games, logs, screenshots
├─ build/                         CMake build trees and exports (build/export/<platform>/)
├─ .gitignore                     /.aegis/  /build/  /CMakeUserPresets.json
└─ .gitattributes                 * text=auto eol=lf, Git LFS patterns for binary content
```

- **The engine owns only these names.** Any other file or folder at the root (`docs/`, `tools/`,
  `tests/`, `screenshots/`, `README.md`) is the user's; the engine never scans, writes or deletes
  it. Unlisted names stay free; a reserved name is not created until it is used.
- **`content/` is the only folder the database scans.** Scenes, materials, models, textures and
  later scripts all live in it, in folders of the user's choosing. The name matches the engine's own
  `content/` folder (both are mounted the same way) and stays clear of the `aegis-assets` library
  and the `aegis.assets` module. Inside it, dot-files and dot-folders are skipped, and files no
  importer handles (`README.md`, `LICENSE.md`, `.blend`, `.psd`) get no sidecar and are no assets.
- **Sources go in as they are, and importing is automatic:** the scan gives every file an importer
  handles its sidecar, and the editor imports it and reimports on any change; there is no manual
  import step to forget. Native files (scenes, materials) are the asset and are edited in place.
- **`project.aegis` has a fixed name,** like Godot's `project.godot`: `--project <dir>` finds it
  without a search, a folder can't hold two, and renaming a game doesn't rename the file. It holds
  the committed settings in sections; `config/` takes sections over only when they outgrow one file
  (per-platform overrides, files tools rewrite often), a versioned migration like any format change.
- **`CMakeLists.txt` is at the root,** where presets, `cmake -S .` and IDEs look. The engine does
  the work through `aegis_add_game`, so the file stays a few lines:

  ```cmake
  cmake_minimum_required(VERSION 4.5)
  project(MyGame CXX)
  add_subdirectory(engine)        # or AEGIS_ENGINE_DIR; find_package(Aegis) later
  aegis_add_game(MyGame SOURCES src/game.cpp PROJECT .)
  ```

  `src/` is optional in the format: a content-only project (once scripting exists) runs on a
  prebuilt player.
- **The engine is a subproject** (a submodule at `engine/`, `FetchContent` or `AEGIS_ENGINE_DIR`).
  The editor links the game module (D7), so a prebuilt, installed editor can't load a game's types
  until game modules are DLLs, and an installed package of C++ modules makes the consumer rebuild
  their BMIs anyway. A submodule also pins the exact engine version; `project.aegis`'s engine
  version becomes a check. An installed package (`find_package(Aegis)`, defining the same
  `Aegis::Engine`) can come with DLL game modules; this answers #134's question for now.
- **`.aegis/` is everything local:** `cache/` is regenerated and "clear cache" deletes only it;
  `editor/` and `saved/` aren't disposable, so they live beside it. A shipped game writes to the
  user's data folder instead (#134, #168).
- **The template's `.gitattributes`** keeps the files the editor writes LF on every platform, so
  sidecars and scenes don't show line-ending diffs on Windows, and lists binary content for Git LFS,
  the normal choice for a game project (not chosen for the engine repository, see below).
- **Paths are case-sensitive on every platform,** Windows included. The database rejects two paths
  that differ only in case, and importers check that every referenced file (e.g. a glTF's texture
  URIs) matches the case on disk, so a mismatch fails on Windows too instead of only on Linux. Paths
  are relative to `content/`, with `/` (`assets.id("models/helmet/Helmet.gltf")`): nothing outside
  it is an asset, so the prefix would carry nothing. Engine content is named through constants; if
  it is ever named by path, paths get a mount prefix (Unreal's `/Game/` and `/Engine/`).
- **Lowercase snake_case** is the convention for engine folders and files the editor creates; it is
  not enforced on imported files, whose names other files (a glTF's URIs) may depend on.
- **Engine content** (default textures, the checker texture, the default cube, the PBR template) is a
  content folder in the engine repository, with normal sidecars and native files, mounted read-only
  next to the project and cooked like project assets. Code names its assets through constants
  (`assets::engine::white`); there are no reserved or special IDs.

### Sample project

The examples, `aegis-test`'s scene checks and the device tests share one sample project,
`aegis-samples`. It is a separate repository, because sample assets are large and the engine
repository should stay small, and is checked out as the submodule `samples/`. It replaces
`aegis-assets`, whose name also clashes with the `aegis-assets` library target (#180).

- **A project like any other.** It follows "Project layout": `project.aegis` once project files
  exist (#132), today's asset folders under `content/`, sorted by kind (`models/` with one folder
  per model, its files as exported; `materials/`, `environments/`), scene files under
  `content/scenes/` (today's `Scenes/`, which holds the examples; stress scenes in `stress/`,
  `aegis-test`'s in `tests/`), and the template's `.gitignore`. Folders the samples create are
  snake_case. The models' README screenshots move out of `content/` to a root `screenshots/`. One
  project for all examples, not one each: Sponza, the helmets and the sky are shared, and the
  examples become scenes in it.
- **Later, with its code:** the Examples game's `src/` and root `CMakeLists.txt` move into the
  sample repository (#167 or later), and the engine `add_subdirectory`s it when it is checked out;
  its `CMakeLists.txt` skips adding the engine when `Aegis::Engine` exists. The samples then test
  the layout and the subproject integration from outside the engine tree.
- **Sidecars are committed** in the sample repository, as in any project. IDs are random, so a
  sidecar that isn't committed gets a new ID on every clone, and scenes reference assets by ID.
- **Opt-in.** `.gitmodules` sets `update = none` and `shallow = true` for it:
  `git clone --recurse-submodules` fetches the libraries but not the samples, and
  `git submodule update --init --checkout samples` fetches them, the pinned commit only. The
  submodule pin still ties every engine commit to the sample commit it was tested with.
- **The engine never needs it.** The libraries, engine content and unit tests build and pass
  without it. CMake builds the examples and registers the device tests that load sample assets only
  when the samples are checked out and the engine is the top-level project; otherwise it prints how
  to opt in. An engine added through `add_subdirectory` never builds the examples.
- **The project root is a run-time input** (`--project`, #132's lookup order). The samples' path,
  fixed at configure time, is only the development default for the examples and `aegis-test`.
  Engine content stays in the engine repository and is mounted next to whichever project runs.
- **Opted in where the samples are needed:** CI (the device tests), and `tools/init-submodules.sh`
  with the `SessionStart` hook, so worktrees and agent sessions have them.

Not chosen: Git LFS in the engine repository (storage and bandwidth quotas, which every CI clone
draws on); downloading an archive at configure time (the pin moves into CMake, and sidecars can't be
edited and committed in place); a copy of the assets per example (shared assets duplicated).

## Identity

**`AssetId` is one `std::uint64_t`** for every asset and every part of a file (a glTF's meshes,
materials, textures): random for a file, derived from it for a part. In files it is exactly 16 lowercase hex digits, leading zeros included;
anything else is rejected on read.

- **64 random bits.** IDs are made on any machine without coordination; at 100,000 assets a
  collision has odds of about 3 × 10⁻¹⁰ (Godot uses 63 bits). 64 bits compare in one instruction and
  hash as they are. The scan and the cook reject duplicates, so a collision can't go unnoticed.
  A file's ID is generated with a `std::mt19937_64` seeded once from `std::random_device`.
- **Sub-assets get their own IDs,** not a sub-index next to the file's ID, so a reference is one ID
  whatever it points to. The importer derives the ID of a new part from the file's ID and the part's
  key, `XXH3_64(le(fileId) ‖ key)`, seed 0, the key as its escaped UTF-8 bytes, and records
  `key → id` in the sidecar. Every machine and branch that meets the same new part writes the same
  line, so merges are clean and a sidecar someone forgot to commit gives nobody other IDs (Unity
  derives model sub-IDs from hashes for this reason). Once recorded, an ID is data and is never
  recomputed. The table lists every part, so the scan finds a part by its ID without importing its
  file. The function is pinned by a test with known outputs, like the entity ID derivation.
- **The ID lives in the file when Aegis owns the format, in a sidecar when it doesn't.** One native
  file is one asset, so moving or committing it can't lose its ID (Unity's classic missing-`.meta`
  bug). Foreign files get `<file>.<ext>.meta` (full name plus `.meta`, so `Helmet.gltf` and
  `Helmet.png` don't collide); a `.slang` or `.lua` asset gets one too. The sidecar picks the
  importer; the extension only gives the default.
- **Duplicates are rejected:** a copied file brings its IDs along, and the scan gives the copy new
  ones, its parts' derived again from its new ID; the editor's "Duplicate" writes new IDs directly.
- **External images are their own assets.** A glTF that references an image file by URI references
  that file's `Texture` ID, resolved through the database relative to the model's folder; only images
  embedded in the model (data URIs, `.glb` buffers) are its sub-assets. One PNG then has one ID and
  one set of import settings, however many models and materials use it, and loads once (Unity,
  Unreal and Godot do the same). The model importer records the usage its materials imply (color or
  linear data) and warns, naming both files, when the image's settings disagree (a normal map
  imported as sRGB); the image's settings win, and the editor will offer to fix them. An image used both ways is imported once per setting
  only on request (not built). An image that isn't a texture asset of the same root is left out of
  its materials with a warning.
- **Files only another file can read are no assets.** A glTF's `.bin` buffers are bytes whose
  meaning is in the glTF's accessors: nothing references, loads or configures them alone, so they
  get no sidecar and no ID. They are dependencies of the model's import, part of its cache key, and a
  change to one reimports every model that reads it. The test for a file type: it is an asset if it
  means something on its own, can be shared or has settings.

```json
{
  "id": "a3f09c2e4b1d7788",
  "importer": "gltf",
  "importerVersion": 1,
  "settings": { "upAxis": "Y", "handedness": "Right", "scale": 1.0, "strictNames": false },
  "subAssets": {
    "StaticMesh/Helmet/0": "1f6b2c0d93ae5547",
    "Material/HelmetMat": "c4d8e1a07b2f9036",
    "Texture/Helmet_normal": { "id": "08e3f5b6a1c47d29", "settings": { "srgb": false } }
  }
}
```

The sidecar's `id` is the file's main asset (a glTF's `Scene`, a PNG's `Texture`).

**Sub-asset keys** match parts across reimports. Names come from the DCC tool, so a bad name never
rejects a file; it warns.
- A key is `<Type>/<name>`, plus `/<index>` for parts that are positions inside another part (a glTF
  primitive: `StaticMesh/<mesh name>/<primitive index>`).
- Embedded images use their glTF image name.
- A duplicate name gets `#<n>` by order of appearance (`Cube`, `Cube#1`), with a warning that
  references follow file order until the parts are renamed. An unnamed part is `#<index>` among its
  type (`Texture/#3`), with the same warning.
- `\`, `/` and `#` inside a name are escaped with `\`.
- The import setting `strictNames` turns the warnings into errors.
- Entries for removed parts stay in the table, so a part that comes back keeps its ID.
- A part renamed in the source gets a new key and a new ID, and references to the old one fall
  back. Editing the old entry's key to the new name by hand keeps its ID. Nothing matches renames
  automatically: guessing by content or position goes wrong too easily.
- **A model's entities** are keyed the same way, as `Entity/<node name>` (`Entity/#root` for the
  root, plus `/<index>` for the entity of one primitive of a mesh with several). Their IDs are the
  local entity IDs of the model's scene, derived like parts' and kept in the same table so they
  survive reimports; they are no assets, so the database leaves them out.

**Settings per part** (#244): an entry is the part's ID, or `{ "id", "settings" }` with the settings
a standalone asset of that type has, so an embedded image's color space (and later its size and
compression) is set in the model's sidecar like an external image's in its own. An embedded image
defaults to the usage its materials imply (base color and emissive sRGB, the others linear); the
model owns it, so nothing can disagree. Meshes and materials can take settings the same way later.
Extracting a part to a file of its own ("Extract material", "Extract textures") keeps its ID and
marks its key as extracted; it is an idea, not planned (#245).

**Finding an asset by ID** goes through an index, the one place paths exist:
- *Development:* the database scans `content/` and the engine content, reading sidecars and native
  headers into `ID → path (+ key)`. It keeps the index in `.aegis/cache/index` with each file's
  modification time and size, so a later start rereads only changed files; the editor's file watcher
  updates it on moves and renames, which leave every reference intact. Until the watcher, the editor
  adds a file it wrote (`AssetDatabase::addFile`) and rescans on request (`rescan`). Every entry
  carries its asset type: from the sidecar's importer, the sub-asset key prefix or the native header.
- *Shipping:* the cook writes a sorted ID table into the pak; no scan, no paths. Godot
  (`uid_cache.bin`), Unity (`Library/`) and Unreal (`AssetRegistry.bin`) do the same.
- The index (where every asset is) is separate from the library's cache (assets now loaded). An
  unknown ID is an error naming the ID and the reference's path hint, if any.

## Native files

JSON (see "Formats and libraries") with a common header:

```json
{
  "type": "Material",
  "id": "5b0e17d2c94af3a1",
  "version": 1,
  "template": "2e7a90c4d15b8f63",
  "parameters": { "roughness": 0.4, "albedoMap": "08e3f5b6a1c47d29" }
}
```

- **The header decides, the extension is for people:** the loader dispatches on `type` through the
  type registry; extensions are plain words (`.scene`, `.material`) for the browser and git.
- **`version` is per file type,** and every registered component type has its own version too. A
  file lists the component types it uses once in its header (`"types": { "PointLight": 2 }`), not on
  every entry.
- **Migrations form a chain:** each type registers one upgrade per version, from n to n + 1, run on
  load until the current version. A version newer than the engine knows is an error.
- **No engine version in asset files:** it would decide nothing the type versions don't, and every
  save after an engine update would change a line in every file. The project file records the
  engine version that last opened the project, for a warning when an older engine opens it.
- **References are `AssetId`s.** A path may sit next to one as a hint for diffs; nothing resolves
  through it.

**Registry names** are the keys of every file, so they follow the C++ names and stay explicit:
types in PascalCase, fields in camelCase without the `m_` prefix, enumerators as in the code. Aliases
for renamed types and fields exist before the first file is saved.

## Scene files

```json
{
  "type": "Scene",
  "id": "9d41c7e0a25b6f18",
  "version": 1,
  "types": { "Transform": 1, "MeshRenderer": 1, "SceneInstance": 1 },
  "entities": [
    {
      "id": "1c5e09b7f3a8d264",
      "name": "Helmet",
      "children": ["7a21e4c90d3b5f86"],
      "components": {
        "Transform": { "position": [0, 0, 1.5], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] },
        "MeshRenderer": { "mesh": "1f6b2c0d93ae5547", "material": "c4d8e1a07b2f9036" }
      }
    }
  ]
}
```

- **A flat list of entities, sorted by local ID,** like the ECS. Reparenting an entity changes only
  the two `children` lists involved.
- **`children` is the only hierarchy data** and holds the sibling order. The parent is derived on
  load, and the loader rejects an entity listed twice, a missing ID or a cycle.
- **Local IDs are random 64-bit IDs,** so two branches adding entities can't collide on merge.
- **World entity IDs:** a world's root scene keeps its local IDs, so an entity's ID in a level file
  is its ID in the running world. Entities of an instance derive theirs from the world ID of the
  entity holding the instance: `XXH3_64(le(holderId) ‖ le(localId))`, seed 0, both values as
  little-endian bytes, chained for nested instances. A script-spawned instance's holder is a new
  entity with a random ID, so its entities derive like any other. The function is pinned by a test
  with known outputs: changing it would break save games and overrides.
- **Save games are full snapshots** of a world (entities by world ID, components, the clock),
  versioned and migrated like scenes, so they survive game updates. A snapshot doesn't pick up level
  content added by an update in places the player already saved; a missing asset falls back to the
  defaults with a warning.

**Scenes are prefabs.** There is no prefab type (Godot's model). An imported glTF is a scene too, so
levels, prefabs and models share one call, `instantiate(world, scene, parent = none)`. The entity
holding an instance carries `SceneInstance { AssetRef<Scene> scene; }`.
1. A level is instantiated into an empty world; switching levels clears the world (or makes a new
   one). The project file names the startup scene; which scenes are levels is down to folders or tags.
2. **Nothing world-level outside entities.** No scene settings section: sky, gravity and the active
   camera are components. For singletons the first wins and duplicates warn.
3. **Overrides (later):** `(localId, component, field) = value` entries next to the instance,
   applied after the child scene. The first version has none.
4. **Unpack** copies an instance's entities into the parent scene and drops the link.
5. **Cycles** fail at load with the chain in the error; the editor refuses to create them.

## Values

Chosen for readability and for catching mistakes, independent of the math library:
- **Vectors and colors** are JSON arrays. Colors are linear; the editor's color picker converts to
  and from sRGB.
- **Rotations** are quaternions, `[x, y, z, w]` (glTF's, Unity's, Unreal's and Godot's order).
  Euler angles exist only in the editor.
- **Units:** meters, radians, seconds. Degrees exist only in the editor. Centimeters would not add
  precision (floats are relative: at 1 km the spacing is about 0.06 mm in any unit), and physics
  engines are tuned for meters; large worlds need doubles or origin rebasing instead.
- **Coordinates:** right-handed, Z-up. Importers convert at import time, per import settings (up
  axis, handedness, scale), baked into the data: every node transform changes basis
  (`T' = C · T · C⁻¹`), and a handedness change also flips the triangle winding. glTF is Y-up and
  right-handed by its spec; the settings matter for OBJ and future formats.
- **Enums** by name; **floats** as the shortest text that round-trips.

## Asset types

The C++ type name is the asset type: the registry name, the header's `type` and the sub-asset key
prefix. `…Data` names CPU import data (`ImageData`, `MeshData`, `ModelData`); the plain name is the
runtime asset an `AssetRef<T>` points to. Components are named for their role, not after the asset
they hold.

| Type | From | Notes |
|---|---|---|
| `Texture` | `.png` `.jpg` `.hdr`, later `.ktx2`, or an embedded image | 2D and cube; sidecar (or the model's entry) holds sRGB, mips, sampler |
| `StaticMesh` | sub-asset of `.gltf` `.glb` `.obj` | vertices, indices, meshlets; `SkinnedMesh` later |
| `Material` | `.material`, or a glTF sub-asset | parameter values for a template (today's `MaterialInstance`) |
| `MaterialTemplate` | engine content (the PBR template) | parameter list and layout; no user files until custom shaders |
| `Environment` | `.hdr` with the environment importer (the `.hdr` default) | sky cube, irradiance, prefiltered maps |
| `Scene` | `.scene`, or a glTF/OBJ import (the file's main asset) | entities; also prefabs and models |

- A glTF needs no runtime `Model` type: it imports as a `Scene` with mesh, material and texture
  sub-assets. `ModelData` exists only inside the importer.
- The BRDF LUT doesn't depend on the environment: it is engine content (or renderer-owned), not part
  of `Environment`.
- Environment baking needs the GPU (`render_graph::Ibl`). It stays at load time for now and can move
  into the editor's import later; the type doesn't change.

**Not assets:** the project file, save games and editor settings (user data), engine shaders (built
from `shaders/`), C++ scripts (attached by registered name).

**Later, with their features:** `Shader` (`.slang`; templates then come from shader reflection),
`Script` (a VM language, with a sidecar), `AudioClip` (`.wav` `.ogg`), `Font` (`.ttf`, to an atlas),
`Skeleton` and `AnimationClip` (glTF sub-assets), `InputMap` (`.input`, from `KeyBindings`).

## Code structure

- **Import** (CPU, no rhi, no scene): source bytes → `ImageData`, `ModelData`. Tested with doctest.
- **Database** (CPU, development builds): scans the project and engine content, owns sidecars, the
  index and the import cache.
- **Library** (`AssetLibrary`, runtime): `load<T>(AssetId)` → `AssetRef<T>`, deduplicated by ID,
  creating GPU objects from imported data. Its source is the project (importing on a miss) or, in a
  shipped game, the pak.

**`AssetRef<T>`**, not `AssetHandle` (`…Handle` means a pooled rhi resource): the ID plus a shared
slot `{state, T}`, pending, ready or failed. The serializer writes only the ID, and the cook step finds
dependencies through `AssetRef` fields in the registry. References are hard (they load with their
owner); soft references, loaded on demand, are a later extension. `load` is async-shaped and
implemented synchronously first; the slot lets hot reload swap contents.
- **Pending:** skip the draw; during a hot reload, keep drawing the old contents. A placeholder for a
  frame or two would look like flicker.
- **Failed or missing:** a loud fallback from engine content, logged once: the default cube for a
  mesh, a magenta and black checker for a texture, the default material with that checker for a
  material.

## Import cache and cooking

- **Cache:** in `.aegis/cache/`, keyed by an XXH3 hash of source bytes, settings, importer version and
  platform, so a changed importer invalidates its own entries. It holds the cooked format: one format
  for development and shipping. A game's import cache is in its project (`.aegis/cache/imported/`),
  or in `$AEGIS_CACHE_DIR/imported/` when that is set; runs without a project (`aegis-test`) use
  `<build>/.aegis/imported/` (`paths::cache()`). The index is still `.aegis/index`.
- **Layout (#137, by key in #238):** `.aegis/cache/imported/<key>/`, one folder per import result
  named by its key (#137 named it by asset ID, one version per file): `model.json` (the key, the
  files the import read besides the source, the node tree, materials, images and the blob of each
  primitive), `mesh-<m>-<p>.mesh` per primitive, `image-<i>.bin` with an embedded image's encoded
  bytes and the empty use marker `used`; the manifests are in `manifests/<source key>.json`. Switching branches back and forth reimports nothing, and two checkouts that point
  `AEGIS_CACHE_DIR` at one folder share it. Entries hold no asset IDs, so identical files share one.
  An external image is an asset of its own: the entry keeps its URI, and changing the image reloads
  the texture, not the model.
- **Textures (#169)** are entries of their own, keyed by the image's bytes and its settings, for
  external and embedded images alike, so changing a texture setting reprocesses that texture only.
- **Lookup in two steps:** the key covers the files the import read besides the source, known only
  after an import. A first key from the source bytes, settings, importer version and platform names
  a small manifest listing those files; their contents then give the entry's key. Both are checked
  on every load, so a changed source, buffer or setting reimports. Identical sources in two folders
  with the same relative buffer paths share the manifest.
- **Publishing:** an entry is written to a staging folder and renamed into place; if a folder with
  that key exists, it is complete and the copy is discarded (unless this load found it damaged: then
  it is replaced). Entries never change once written, so a second process never reads half an entry.
- **Eviction:** each use touches a marker in the entry (file access times are unreliable and often
  off on NTFS); entries unused for some time or past a size limit are deleted, and a delete that
  fails because another process has the file open is skipped: the entry is renamed aside before it
  is deleted, so it goes whole or not at all. Each `AssetLibrary` start evicts entries unused for 30
  days, then the least recently used ones until the rest fit in 8 GiB (`ImportCache::EvictDesc`);
  manifests unused for 30 days and staging folders left for an hour go too.
  "Clear cache" deletes everything. A shared cache (a network folder or server keyed the same way)
  needs no format change later.
- **Reimport:** the editor watches the project; a start without the editor checks the keys. The unit
  of reimport is a file: its parts are matched by key, matched parts' slots get the new contents,
  removed parts fall back and new parts are added (hot reload, #143). The manifest maps a changed
  `.bin` to the models that read it.
- **Cook** (`<Name>-Editor --cook <out>`): follow `AssetRef`s from the project's scenes (plus an
  "always include" list), import what the cache lacks, compile native files to binary, pack with an
  ID table. Importers then leave the shipped executable.

## Formats and libraries

**Text: JSON through glaze,** for scenes, materials, sidecars and the project file.
- **Stable output:** pretty-printed, one key per line, a fixed key order, entities sorted by local
  ID. Saving an unchanged world gives the same bytes, and diffs stay small. Comments and trailing
  commas are accepted on read (JSONC), never written.
- **Two paths:** fixed-shape files (sidecars, the project file, import settings) bind structs through
  glaze's reflection. Scene files go through the type registry as a generic tree, since component
  types are known only at runtime.
- **Asset references:** `reflect::FieldKind` gains an `AssetRef` kind, written as the ID; the cook
  step finds dependencies through it (#130 adds it, #131 serializes it).
- **Why not YAML:** implicit typing traps hand edits, glaze's YAML covers less than its JSON (no
  documented generic tree or error locations), and editor saves drop comments in either format.
- The format is chosen in one place in the serializer. glaze's templates are heavy: include it only
  in `.cpp` files. Its errors carry their location, which gives #131's line numbers.

**Binary (cache and pak):**
- **Textures:** KTX2 (mips, cubes, arrays, the `VkFormat`, zstd), readable by standard tools.
- **Meshes (#137):** own blobs, a 64-byte header `{magic "AEGM", type, version, cache key, counts,
  bounds}`, then the vertices, indices, meshlets, meshlet vertices and meshlet primitives as
  `StaticMesh` uploads them, each starting at a multiple of 16 bytes. Little-endian and uncompressed
  for now; meshoptimizer's vertex/index codecs and zstd come later, under a new version.
- **Model entries** are JSON (glaze) until models load as scenes (#140).
- **Cooked scenes and save games** stay JSON at first; glaze's BEVE is the binary option.
- **Debugging:** `<Name>-Editor --dump-asset <id>` prints a cache entry as text (#137), found
  through the asset's current key.

**Libraries,** as submodules under `external/`, linked `PRIVATE`, all usable without exceptions:

| Need | Library |
|---|---|
| JSON | glaze (yyjson is the fallback if glaze's templates fight Clang modules) |
| Hashing | xxHash (XXH3_64: cache keys and entity ID derivation) |
| Compression | zstd |
| Textures | KTX-Software (libktx), with Basis Universal for per-platform BC7 / ASTC |
| Mesh compression | meshoptimizer (already a dependency) |
| File watching | efsw (#143) |
| Decoding | stb_image, fastgltf, tinyobjloader (kept; importers only, so they leave shipped games) |

No library for random IDs (`std::mt19937_64`), the pak (a sorted table plus memory-mapped reads) or a
job system (not before loading goes async). First check: one `.cpp` that includes glaze and does
`import std;` builds on `windows-clang` and `linux-clang` (libstdc++ 15).

## Changes and order

Renames:
- `scene::Scene` → `World`, and `aegis.scene` → `aegis.world`, mechanical, right after 6.2 (#80):
  6.14 (#138). `Scene` becomes the serializer's asset type (6.10, #131).
- 6.15 (#139): the `Mesh` and `Material` components merge into
  `MeshRenderer { AssetRef<StaticMesh> mesh; AssetRef<Material> material; }` (every pass views both
  anyway, and D2's signals then watch one component); `MaterialInstance` → `Material`; the
  `Environment` component → `Sky { AssetRef<Environment> environment; }`, without the BRDF LUT.
- `Engine::loadModel` / `loadTexture` / `loadEnvironment` → `AssetLibrary` calls by ID (#130);
  `core::Asset` goes (#75).

Order:
1. #72 → #73 (defines the sub-asset keys) → #74 → #75; #138 after #80; #139 after #74.
2. 6.9 (#130): IDs, sidecars, the database and engine content, `AssetRef`, the library cache
   (closes A-CONTENT-04).
3. 6.13 (#137): the import cache; then derived sub-asset IDs (#236) → settings per part (#244),
   and the cache by key (#238) before KTX2 textures (#169).
4. 6.10 (#131): scene files.
5. 6.16 (#140): scene instances with derived entity IDs; models load as scenes.
6. Export (#134): cook and pak.
7. Later: material files (#141), overrides and unpacking (#142), hot reload (#143). An idea, not
   planned: extracting parts to their own files (#245).

**Golden files:** a small scene, a material and a sidecar under `tests/`, with tests that must keep
loading them unchanged (#130 for the sidecar and IDs, #131 for scene and material files). An
accidental format change then fails CI instead of breaking a project.

## TBD (cheap to change, decided when built)

- Blob formats other than meshes.
- The pak format and its ID table.
- Blob compression settings; where per-platform texture compression runs (import or cook).
- The library's internals and when loading becomes async.
- glaze or the yyjson fallback (the files stay JSON either way).

## Open

- `.blend` import through headless Blender, which every importing machine would need.
- Override details; a `.prefab` extension for the browser (same format either way).
- Soft references, hot reload and streaming.
- Game shaders: content (imported and compiled by the asset pipeline, as material shaders would be)
  or source (`src/`, compiled by CMake through `aegis_add_shaders`, as the engine's are).
- Export metadata (window title, version, executable icon) in `project.aegis`, the icon in
  `content/`; the Windows icon is needed at build time, so the build reads the project file.
