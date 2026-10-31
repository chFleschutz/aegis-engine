# Aegis Engine

A cross-platform C++23 Vulkan render engine that hosts games. This glossary says what its terms mean; what to do with them is in AGENTS.md.

## Language

### Code

**New style**:
The conventions of the code rewritten onto the RHI. AGENTS.md lists them and the folders they cover.
_Avoid_: modern code, new code

**Legacy**:
The older conventions of the code around it, from before that rewrite.
_Avoid_: old style, old code

### GPU resources

**Handle**:
A pooled, generational reference to a GPU resource, resolved through `Device::get`: `BufferHandle`, `ImageViewHandle`, `SamplerHandle`.
_Avoid_: descriptor handle, resource pointer

**Descriptor**:
A bindless slot index that a shader consumes: `Descriptor<T>`, as in `SampledImageDescriptor`, `StorageImageDescriptor`, `SamplerDescriptor`.
_Avoid_: descriptor handle, bindless handle, slot handle

**Id**:
A plain value that identifies something with no pool behind it: a vector index (`render_graph::ResourceId`) or a stable identity (`AssetId`).
_Avoid_: handle

### World

**Static entity**:
An entity without a `DynamicTag`: frozen after `begin`, and part of the static set the renderer keeps.
_Avoid_: fixed entity, immovable entity

**Dynamic entity**:
An entity with a `DynamicTag`, free to move in any frame.
_Avoid_: movable entity

### Games

**Game**:
A game module plus a project: the code and the data of one title.
_Avoid_: app, application

**Game module**:
A game's code as the launcher sees it: a `GameModule`, defined once with `AEGIS_GAME_MODULE` and statically linked.
_Avoid_: game plugin, game DLL

**Flavor**:
One of a game's two executables: the runtime `<Name>`, or the editor `<Name>-Editor`, the only one that links the editor.
_Avoid_: build type, target

**Launcher**:
The engine's `main()`, `src/aegis/launch/main.cpp`, shared by both flavors of every game.
_Avoid_: bootstrap, game main

**Shell**:
What decides, each frame, what the app shows around the game: its UI, which worlds update and render, and who receives input.
_Avoid_: layer, mode

**GameShell**:
The runtime's shell and the engine's default: the game plays in the main world and gets all the input, with no UI around it.

**EditorShell**:
The editor flavor's shell: it keeps the world in edit mode, where no scripts run, and builds the editor's UI around it.

**Safe point**:
The moment in each frame when the shell runs: input has been read, and no world is being updated or rendered.

### Projects and assets

**Project**:
A game's folder of data, found by the launcher. The sample project is the `samples/` submodule.
_Avoid_: game folder, workspace

**Project content**:
The `content/` folder of a project, which holds its assets.
_Avoid_: project assets, assets folder

**Engine content**:
The read-only assets that come with the engine, from the repository's `content/` folder.
_Avoid_: built-in assets, default assets

**AssetId**:
The random 64-bit identity of an asset or of a part of a file.
_Avoid_: GUID, UUID, asset handle

**AssetRef**:
A reference to an asset: its `AssetId` plus the shared slot the library loads it into.
_Avoid_: asset handle, asset pointer

**Sub-asset**:
A part of a model file with an `AssetId` of its own: a mesh, a material or an embedded texture.
_Avoid_: sub-resource, embedded asset

**Sidecar**:
The `<file>.<ext>.meta` file next to a foreign source file, holding its `AssetId`, its import settings and its sub-assets' IDs.
_Avoid_: meta file, metadata file

### Tests

**Device test**:
A CTest test labelled `device`, which needs a live `rhi::Device`.
_Avoid_: GPU test, integration test

**Golden test**:
A device test labelled `golden`, which compares the last frame with a reference image generated on the dev GPU.
_Avoid_: screenshot test, reference test
