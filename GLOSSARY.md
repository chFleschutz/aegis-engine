# Aegis Engine

A cross-platform C++23 Vulkan render engine that hosts games. This glossary says what its terms mean; what to do with them is in AGENTS.md.

## Language

### Code

**New style**:
The conventions of the code rewritten onto the RHI, now everywhere but the legacy rest. AGENTS.md lists them and names that rest.
_Avoid_: modern code, new code

**Legacy**:
The older conventions, from before that rewrite, kept by the code it hasn't reached yet.
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

### Frame graph

**Scene passes**:
The passes that render one view of the scene into its "Final" (`addScenePasses`); a game's graph hook adds to them or replaces them.
_Avoid_: default passes (those are the scene passes plus the output passes)

**Scope**:
A named group of passes in the frame graph whose resources are named after it ("Main/Depth"); its passes find their scope's resources first, then the unscoped ones. Each view's scene passes run in a scope of the view's name.
_Avoid_: subgraph, namespace, prefix

**Output passes**:
The passes that show the views' "Final" once everything has drawn onto them: UI and present (`addOutputPasses`). The engine adds them last, after the game's and the shell's passes.
_Avoid_: final passes

**Persistent texture**:
A graph texture whose contents survive the frame: what a pass leaves in it is there in the next frame.
_Avoid_: static texture, retained texture

**History pair**:
Two images behind one graph texture, swapped every frame: passes write the current image and read the previous frame's.
_Avoid_: ping-pong texture, double buffer

**Imported resource**:
An image or buffer owned outside the graph, declared with the access it comes from when a frame starts and the one the graph leaves it ready for.
_Avoid_: external resource

**Fresh**:
Said of a persistent texture or history pair that holds nothing of the previous frame yet: in the first frame, after a resize recreated it and after a graph rebuild.
_Avoid_: invalidated, reset

### Rendering

**Frame scene**:
What the passes draw of one world in one frame: its instances, its views, the lights, the sky's environment and the post-processing settings.

**View**:
One rendering of the world in a frame: a camera, an extent and a kind, main or shadow. Without a shell's views the main camera's view, "Main", renders onto the window; a shell's views each render into a target of their own.
_Avoid_: render view (the shell's request for one, `RenderView`), viewport (the editor's panel), camera

**Shadow view**:
A view of the depth of the world as a light sees it, its shadow map, which a main view's lighting samples. Each main view has one for the directional light, fitted to its camera ("MainShadow").
_Avoid_: shadow pass (its passes), shadow camera (its camera), light view

**Material template**:
The parameters a kind of material has, with their defaults, and the surface shader that reads them.
_Avoid_: material type, shader template

**Material**:
A material template's parameter values, which meshes draw with.
_Avoid_: material instance

**Material variant**:
What decides how a material draws: its template, alpha mode and cull mode. Materials of one variant draw with the same pipelines.
_Avoid_: material permutation, material kind

**Draw batch**:
The instances of one material variant, drawn together with one pipeline.
_Avoid_: material batch, bucket

**Technique**:
How a pass draws a material: into the G-buffer, or its depth only (a shadow view); forward later.
_Avoid_: render mode, shader pass

**Pipeline key**:
Everything that picks one pipeline for a draw batch: the technique, the material variant, the geometry path and the front face.
_Avoid_: pipeline id, pipeline hash

**Surface shader**:
A material template's shader code: it turns a material's parameters into the properties of a surface (albedo, normal, ARM, emissive, alpha), and every technique calls it.
_Avoid_: material shader

**Shading model**:
How the lighting pass shades a G-buffer pixel: lit or unlit.
_Avoid_: lighting model

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
