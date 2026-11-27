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

**Pass settings**:
A pass's runtime tunables (the lighting view mode, the bloom filter scales): a `Settings` struct registered with the type registry in the Settings category, which the pass reads every frame and tools set by field name. The engine keeps all of them in one `RenderSettings`, which outlives its frame graphs and every view's passes read. Not scene data: post-processing lives on the camera's `PostProcess` component.
_Avoid_: pass options, debug UI

### Rendering

**Frame scene**:
What the passes draw of one world in one frame: its instances, its views, the lights, the sky's environment and the post-processing settings.

**View**:
One rendering of the world in a frame: a camera, an extent and a kind, main or shadow. Without a shell's views the main camera's view, "Main", renders onto the window; a shell's views each render into a target of their own.
_Avoid_: render view (the shell's request for one, `RenderView`), viewport (the editor's panel), camera

**Shadow view**:
A view of the depth of the world as a light sees it, its shadow map, which a main view's lighting samples. Each main view has one per cascade of the directional light ("MainShadow0" to "MainShadow3"); the frame's closest shadow-casting spot lights each have one ("SpotShadow0" to "SpotShadow3"), which all main views share.
_Avoid_: shadow pass (its passes), shadow camera (its camera), light view

**Shadow atlas**:
One depth texture holding the spot lights' shadow maps, a tile per spot shadow view ("SpotShadowAtlas"); a spot light's light record names its tile.
_Avoid_: shadow texture array, shadow page, tile map

**Shadow cascade**:
One slice of a main view's camera axis, up to the directional light's shadow distance, with a shadow view fitted to it; nearer cascades are shorter, so their shadows are sharper.
_Avoid_: split (the distance where one ends), cascade level, shadow layer

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
How a pass draws a material: into the G-buffer, its depth only (a shadow view), or forward: lit and blended into the scene color, as the forward pass draws blended materials, which the G-buffer can't hold.
_Avoid_: render mode, shader pass

**Pipeline key**:
Everything that picks one pipeline for a draw batch: the technique, the material variant, the geometry path and the front face.
_Avoid_: pipeline id, pipeline hash

**Pipeline library**:
The engine's one store of the passes' pipelines and the BRDF lookup table, which outlives every frame graph: each view's passes and each rebuilt graph take their pipelines from it, so a pipeline is created once.
_Avoid_: pipeline cache (a pass's lookup by pipeline key), pipeline pool

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

**Fixed step**:
A step of the simulation with the same length every time (`World::fixedStep`, 1/60 s by default), decoupled from the frame rate: an update runs zero or more of them in `Phase::FixedUpdate` (`onFixedUpdate`), and `alpha` is the fraction of a step left over.
_Avoid_: tick, physics frame

**World time**:
A world's `Time`, which its update writes and its systems, scripts and the renderer read (`World::time`, `Registry::time`): the frame delta, the fixed step, `alpha`, the elapsed time and the frame and step counts. The world's `FrameClock` only hands out the frame deltas.
_Avoid_: game time, scene time

**Event**:
A plain struct that the systems and scripts of one world send each other through its `Events` (`Registry::events`, `ScriptContext::emit`): emitted during an update, read by every reader during the next one, then dropped. Events are not saved, so a type needs no registration.
_Avoid_: message, signal (entt's signals are component observers)

**Spawn**:
Instantiating a scene or model asset from a script at run time (`ScriptContext::spawn`), at the start of the next update: a new holder entity, whose StableId the script gets at once, holds the instance. The engine's `scripting::Spawner` does it through the asset library.
_Avoid_: instantiate (for the script's call), create

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
The engine's `main()`, `aegis/launch/main.cpp`, shared by both flavors of every game.
_Avoid_: bootstrap, game main

**Shell**:
What decides, each frame, what the app shows around the game: its UI, which worlds update and render, and who receives input.
_Avoid_: layer, mode

**GameShell**:
The runtime's shell and the engine's default: the game plays in the main world and gets all the input, with no UI around it.

**EditorShell**:
The editor flavor's shell: it keeps the world in edit mode, where no scripts run, and builds the editor's UI around it.

**Workspace**:
The editor's panels and where each one sits: docked in the layout, in the right column or its side rail, in the drawer or pinned below the viewport, or floating.

**Safe point**:
The moment in each frame when the shell runs: input has been read, and no world is being updated or rendered.

**Input action**:
A named button a game reads instead of a key ("Jump"): down while any of its inputs (keys, mouse buttons, gamepad buttons or axes) is.
_Avoid_: action binding, key binding (`KeyBindings` are the editor's shortcut callbacks)

**Input axis**:
A named value from -1 to 1 a game reads instead of keys ("MoveX"), from a pair of inputs or a gamepad axis.

**Input map**:
A project's input actions and axes, `platform::InputMap`, from its project file. `InputState` reads them by name.
_Avoid_: action map, input settings

### Audio

**AudioClip**:
A sound decoded into memory, `assets::AudioClip`: an asset from a `.wav` or `.ogg` file.
_Avoid_: sound file, sample

**AudioSource**:
The component that plays an `AudioClip` from its entity's position, started on begin or by `audio::play`.
_Avoid_: emitter, sound component

**One-shot**:
A clip played once at a position with no entity behind it (`audio::playOneShot`), for scripts.

**AudioEngine**:
The engine's one mixer, `audio::AudioEngine`, that every world's `AudioSystem` plays through. Headless it has no device and advances by frame time.
_Avoid_: sound manager, audio device

### Physics

**Body**:
An entity's presence in its world's physics, made by the `PhysicsSystem` for each entity with a `Collider` while the world plays. Its motion type is static, kinematic or dynamic.
_Avoid_: physics object, actor

**Collider**:
The component that gives an entity's body its shape (box, sphere or capsule) at an offset, scaled by the entity's global scale. A `Collider` alone makes a static body.
_Avoid_: collision shape, hitbox

**RigidBody**:
The component that sets a body's motion type, mass and surface: friction, restitution, damping and gravity factor.
_Avoid_: physics component

**Static body**:
A body that never moves and that others collide with. Not the same as a static entity: a static body's entity may still move, its body stays put.
_Avoid_: fixed body

**Kinematic body**:
A body that follows its entity's `Transform` (a script moves it) each fixed step and pushes dynamic bodies, unaffected by them.

**Dynamic body**:
A body moved by gravity and collisions; the `PhysicsSystem` writes its pose, blended by `alpha`, into its entity's `Transform`.

**Character**:
An entity the `PhysicsSystem` walks through the world for its `CharacterController`: an upright capsule standing on the entity's location, moved each fixed step by the controller's desired velocity and gravity. It slides along walls, climbs steps and pushes dynamic bodies. Not a body, though it has an inner body that bodies and queries meet.
_Avoid_: player controller, character body

**Contact event**:
`ContactBegin` or `ContactEnd`, the event the `PhysicsSystem` emits after a fixed step when two entities' bodies (a character's too) start or stop touching. A pair that stays in contact sends one begin, also while its bodies sleep.
_Avoid_: collision event, collision callback

**Physics query**:
A question to a playing world's physics through its registry: `physics::raycast` for the first body along a ray, `physics::overlapSphere` and `physics::overlapBox` for the entities whose bodies overlap a volume.
_Avoid_: trace, physics test

### Projects and assets

**Project**:
A game's folder of data, found by the launcher. The sample project is the `games/examples/project/` submodule.
_Avoid_: game folder

**Project file**:
A project's settings, `<project>/project.aegis`, read when the engine starts. So far it holds only the input map.
_Avoid_: project settings, project manifest

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

**Scene instance**:
The entities a scene asset (a prefab, a model) produced in a world, below the holder, which keeps a link to that scene and recreates them from it.
_Avoid_: prefab instance

**Holder**:
The entity a scene instance hangs below, which saves as the link to the scene instead of the instance's entities.

**Override**:
A field of a scene instance's entity set to a value of the instance's own, stored on the holder by the entity's local ID, component and field, and written over the scene's value.
_Avoid_: modification, property override

**Unpack**:
Turning a scene instance's entities into plain entities of the scene that holds it, dropping the link and its overrides.
_Avoid_: break prefab, make local

### Tests

**Device test**:
A CTest test labelled `device`, which needs a live `rhi::Device`.
_Avoid_: GPU test, integration test

**Golden test**:
A device test labelled `golden`, which compares the last frame with a reference image generated on the dev GPU.
_Avoid_: screenshot test, reference test
