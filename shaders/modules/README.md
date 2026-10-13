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

[[vk::push_constant]] ConstantBuffer<DrawConstants> constants;
```

On the C++ side, mirror the struct with `rhi::DeviceAddress` for pointers and `std::uint32_t` for
indices, and record it with `cmd.pushConstants(value)`. The struct must fit in 128 bytes, which the
typed `pushConstants` checks at compile time.

## Layout: scalar, so C++ structs match

Every shader compiles with `-fvk-use-scalar-layout` (see `shaders/CMakeLists.txt`): push constants
and everything read through a pointer are packed like C++ structs, with each member aligned to its
component type (a `float3` takes 12 bytes, aligned to 4). So a C++ struct of `glm::vec3`, `float` and
`rhi::DeviceAddress` matches the Slang struct member for member — `assets::Vertex`, `StaticMesh::GpuData`
and material parameter blocks rely on it. Matrices are row-major (`-matrix-layout-row-major`); upload
`glm::rowMajor4(m)`.

Working examples: `computeMain` in `src/aegis/test/shader.slang`, and `shaders/ibl/*.slang`
(driven by `render_graph::Ibl`).
