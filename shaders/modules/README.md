# Shader interface

How shaders receive their data from the RHI (`aegis::rhi`). Shaders live in `shaders/` and share the
modules in this folder through `import modules.*`.

## One pipeline layout for every pipeline

`rhi::Device` creates a single pipeline layout, and every `rhi::Pipeline` uses it:

| Part                    | Contents                                                          |
|-------------------------|-------------------------------------------------------------------|
| Set 0, binding 0        | sampled images of any view type (`SampledImageDescriptor` indices)  |
| Set 0, binding 1        | storage images of any view type (`StorageImageDescriptor` indices)  |
| Set 0, binding 2        | samplers (`SamplerDescriptor` indices)                              |
| Push constants          | 128 bytes at offset 0, visible to all stages                      |

`CommandBuffer::bindPipeline` binds set 0 automatically. `modules/bindless.slang` declares the
arrays (aliased per resource type, e.g. `Texture2D` and `TextureCube` over binding 0) and typed
accessors: `bindless::texture2D(SampledImageDescriptor)`, `bindless::storage2DArray(StorageImageDescriptor)`,
`bindless::sampler(SamplerDescriptor)`. Images and samplers are separate; `bindless::TextureDescriptor`
pairs them, which is how an `assets::Texture` reaches a shader (8 bytes: image index, sampler index).
Storage images are declared without a format, so one binding serves every format.

## Buffers: device addresses in push constants

Buffers have no descriptors. A shader receives a buffer's `Buffer::deviceAddress()` as a pointer in
its push constants (the buffer needs `BufferUsage::ShaderDeviceAddress`). Descriptor indices travel the
same way. One `[[vk::push_constant]]` struct per entry point:

```slang
import modules.bindless;

struct DrawConstants
{
    float4x4* transforms;                // buffer device address
    Vertex* vertices;                    // buffer device address
    bindless::TextureDescriptor albedo;  // assets::Texture's image and sampler indices
}

[[vk::push_constant]] ConstantBuffer<DrawConstants> push;
```

A struct reached through a pointer keeps its own pointer members as `uint64_t` addresses behind
properties (see `common::Mesh`): SDK 1.4.328's slangc emitted invalid SPIR-V for a struct-pointer
member there (not re-checked with 1.4.363).

On the C++ side, mirror the struct with `rhi::DeviceAddress` for pointers and the typed
`rhi::*Descriptor` structs for descriptor indices, and record it with `cmd.pushConstants(value)`. The
struct must fit in 128 bytes, which the typed `pushConstants` checks at compile time. The render graph's
mirrors live in `src/aegis/render_graph/gpu_types.cppm`, named after their Slang twins, with their
`sizeof`/`offsetof` pinned in `tests/render_graph/gpu_types.test.cpp`; asset mirrors (`Vertex`,
`Meshlet`) are in `src/aegis/assets/data/gpu_types.cppm`. A material's parameter block has no C++ mirror: its
material template lists the parameters, and the template loader checks them against the surface shader's
reflected block (see `modules/surface.slang`).

**Declare the push constant variable in the entry-point file, never in a module.** A module may define
the push constant *struct* (`indirectDraw::GeometryConstants`), but each entry point's file declares the
`[[vk::push_constant]]` variable. A variable at module scope is visible to every importer: an entry
point that also declares its own would statically use two, which Vulkan forbids (spirv-val reports
VUID-StandaloneSpirv-OpEntryPoint-06674), and the module's helpers would read the wrong layout. Module
helpers take what they need as parameters instead (`indirectDraw::getInstance(staticInstances, ...)`).

## Layout: scalar, so C++ structs match

Every shader compiles with `-fvk-use-scalar-layout` (see `cmake/AegisShaders.cmake`, which every shader goes through): push constants
and everything read through a pointer are packed like C++ structs, with each member aligned to its
component type (a `float3` takes 12 bytes, aligned to 4). So a C++ struct of `glm::vec3`, `float` and
`rhi::DeviceAddress` matches the Slang struct member for member — `assets::Vertex`, `StaticMesh::GpuData`
and material parameter blocks rely on it. Matrices are row-major (`-matrix-layout-row-major`); upload
`glm::rowMajor4(m)`.

Working examples: `computeMain` in `src/aegis/test/shader.slang`, and `shaders/ibl/*.slang`
(driven by `render_graph::Ibl`).

## Lighting

`modules/lighting.slang` holds the shading by a view's lights: `lighting::shade` lights a `SurfacePoint` with the
ambient light (the sky's `Environment` maps), the directional light and the point and spot lights of a
`lighting::Lighting` block (`render_graph::LightingUniforms`). The lighting pass (`lighting.slang`) shades G-buffer
pixels with it; any other pass that lights surfaces calls it too rather than copying it, so all passes shade alike.

## Techniques and surface shaders

A material draws through its material template's surface shader (`surfaces/<name>.slang`), which implements
`ISurface` from `modules/surface.slang`: its `Params` struct is the material's parameter block, `evaluate` turns
it into a `SurfaceOutput` (albedo, tangent-space normal, AO/roughness/metallic, emissive, alpha), and
`shadingModel` says whether the lighting pass lights it. Each surface module ends with `public typealias Surface =
<its type>;`.

A technique (`cpu-driven/geometry.slang`, `gpu-driven/geometry.slang`: the G-buffer, today) does `import
SURFACE_MODULE;` and names only `Surface`. It gets a material as a `MaterialAddress`, which only
`evaluateSurface<Surface>` reads, as `Surface.Params*`: no technique casts the address itself. `shaders/CMakeLists.txt` lists the techniques; `aegis_add_shaders`
builds each one once per surface shader, with `-DSURFACE_MODULE=surfaces.<name>`, into
`<technique>.<name>.spv`, and again with `-DALPHA_MASK` into `<technique>.<name>.masked.spv`, and reflects each surface
shader's block into `surfaces/<name>.layout.json`. A render pass picks the file for a batch's material variant with
`render_graph::techniqueShader`.

**Masked and double-sided materials.** A technique's masked build calls `alphaTest`, which discards a fragment whose
`SurfaceOutput.alpha` is below its `alphaCutoff` (the PBR surface: the albedo map's alpha times the `alpha`
parameter, against the `alphaCutoff` parameter); only masked materials (`AlphaMode::Mask`) draw with it, so opaque
pipelines keep early depth testing. A material that culls nothing (`rhi::CullMode::None`, glTF's `doubleSided`)
draws back faces too: techniques build their `SurfaceInput` with `toSurfaceInput`, which flips the normal on a back
face.

To add a surface shader, add `surfaces/<name>.slang` and a template naming it (`"surface": "<name>"`) whose
parameters list the `Params` fields in order: the template loader rejects a template whose parameters differ from
the reflected block, naming the parameter.
