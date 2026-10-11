# Shader interface (new RHI)

How shaders written for the new RHI (`aegis::rhi`) receive their data. `bindless.slang` in this
folder belongs to the **old** stack (`Aegis::Graphics`) and uses different binding slots; a Slang
module for the layout below arrives with the Phase 2 shader port (see `docs/rhi-migration-plan.md`).

## One pipeline layout for every pipeline

`rhi::Device` creates a single pipeline layout, and every `rhi::Pipeline` uses it:

| Part                    | Contents                                                          |
|-------------------------|-------------------------------------------------------------------|
| Set 0, binding 0        | sampled images of any view type (`SampledImageDescriptor` indices)  |
| Set 0, binding 1        | storage images of any view type (`StorageImageDescriptor` indices)  |
| Set 0, binding 2        | samplers (`SamplerDescriptor` indices)                              |
| Push constants          | 128 bytes at offset 0, visible to all stages                      |

`CommandBuffer::bindPipeline` binds set 0 automatically. Images and samplers are separate, so a
shader combines a texture index with a sampler index itself.

## Buffers: device addresses in push constants

Buffers have no descriptors. A shader receives a buffer's `Buffer::deviceAddress()` as a pointer in
its push constants (the buffer needs `BufferUsage::ShaderDeviceAddress`). Descriptor indices travel the
same way. One `[[vk::push_constant]]` struct per entry point:

```slang
struct DrawConstants
{
    float4x4* transforms;     // buffer device address
    Vertex* vertices;         // buffer device address
    uint albedoTexture;       // SampledImageDescriptor::index
    uint albedoSampler;       // SamplerDescriptor::index
};

[[vk::push_constant]] ConstantBuffer<DrawConstants> constants;
```

On the C++ side, mirror the struct with `rhi::DeviceAddress` for pointers and `std::uint32_t` for
indices, and record it with `cmd.pushConstants(value)`. The struct must fit in 128 bytes, which the
typed `pushConstants` checks at compile time. Slang lays push constants out with std430 rules, so
order the members largest-alignment first (pointers, then 4-byte values) and the C++ struct matches
without padding surprises.

Working example: `computeMain` in `src/aegis/test/shader.slang` and `verifyComputeRoundTrip` in
`src/aegis/test/main.cpp`.
