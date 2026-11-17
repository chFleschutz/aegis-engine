# Asset system: sources in the project, imported data in a cache

Formerly D8. Decided 2026-10-19, see
[`docs/asset-system.md`](../asset-system.md).

- **Identity:** `AssetId` is one random 64-bit ID (16 lowercase hex digits in files) for every
  asset and every part of a file, found through an index (scanned in development, a pak table when
  shipped). Foreign source files (glTF, OBJ, PNG, HDR) get a `<file>.<ext>.meta` sidecar with the
  ID, import settings and a `key → id` table for their parts; native files (scenes, materials) carry
  their ID in a common `{type, id, version}` header. Engine content is a read-only folder of normal
  assets, with no reserved IDs.
- **Fixed formats (2026-10-20):** JSON through glaze; per-type versions with single-step migration
  chains; random local entity IDs, with instance entity IDs derived by XXH3-64; a flat entity list
  with `children` arrays; quaternions `[x, y, z, w]`, meters, radians, linear colors, right-handed
  Z-up converted at import; paths case-sensitive everywhere. Pinned by golden-file tests.
- **Import cache:** in `<project>/.aegis/`, the only git-ignored folder, keyed by a hash of source,
  settings, importer version and platform. Its format is the cooked format, so cooking packs it.
- **Runtime:** `AssetLibrary::load<T>(AssetId)` returns an `AssetRef<T>` (a shared slot with a load
  state), deduplicated by ID. Components store `AssetRef`s; the serializer writes only IDs.
- **Scenes and worlds:** a `World` is the live object, a `Scene` the asset. Prefabs are scenes, not
  a type; `instantiate(world, scene, parent)` serves levels, prefabs and imported models. Scene
  files store local entity IDs, and world IDs derive from instance and local ID. "Level" is not an
  engine type. `scene::Scene` becomes `World` and `aegis.scene` becomes `aegis.world`.
- **Asset types:** `Texture`, `StaticMesh`, `Material` (was `MaterialInstance`),
  `MaterialTemplate` (engine content), `Environment`, `Scene`. The `Mesh` and `Material` components merge
  into `MeshRenderer`, and the `Environment` component becomes `Sky`.
- Why: source stays authoritative and reimports itself; imported data, which depends on importer
  version and shader-pinned layouts, is never committed, so a layout change needs no migration.
  Sidecars survive moves and git merges, unlike paths or one database file.
- → the renames ([#138](https://github.com/chFleschutz/aegis-engine/issues/138), [#139](https://github.com/chFleschutz/aegis-engine/issues/139), done), then [M6](https://github.com/chFleschutz/aegis-engine/milestone/6); export ([#134](https://github.com/chFleschutz/aegis-engine/issues/134)); later save games
  ([#168](https://github.com/chFleschutz/aegis-engine/issues/168)), material files ([#141](https://github.com/chFleschutz/aegis-engine/issues/141)), prefab overrides ([#142](https://github.com/chFleschutz/aegis-engine/issues/142)) and hot reload ([#143](https://github.com/chFleschutz/aegis-engine/issues/143)). Decides
  F2.
