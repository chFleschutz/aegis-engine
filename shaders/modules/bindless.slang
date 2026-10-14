module bindless;

// Set 0 of the pipeline layout every pipeline shares: the RHI's bindless heap (rhi::BindlessHeap).
// It has one binding per descriptor type; the arrays below alias those bindings with the resource
// types shaders need. A descriptor index from C++ (rhi::SampledImageDescriptor and friends) selects
// the element. See shaders/modules/README.md.

[allow("parameterBindingsOverlap")] [vk::binding(0, 0)] Texture2D g_texture2D[];
[allow("parameterBindingsOverlap")] [vk::binding(0, 0)] TextureCube g_textureCube[];
[allow("parameterBindingsOverlap")] [vk::binding(0, 0)] Texture2DArray g_texture2DArray[];

// Storage images are declared without a format (shaderStorageImage{Read,Write}WithoutFormat).
[allow("parameterBindingsOverlap")] [vk::binding(1, 0)] RWTexture2D<float4> g_storage2D[];
[allow("parameterBindingsOverlap")] [vk::binding(1, 0)] RWTexture2DArray<float4> g_storage2DArray[];

[vk::binding(2, 0)] SamplerState g_samplers[];

namespace bindless
{
    public struct SampledImageDescriptor
    {
        public uint index;
    }

    public struct StorageImageDescriptor
    {
        public uint index;
    }

    public struct SamplerDescriptor
    {
        public uint index;
    }

    public func texture2D(SampledImageDescriptor descriptor) -> Texture2D
    {
        return g_texture2D[NonUniformResourceIndex(descriptor.index)];
    }

    public func textureCube(SampledImageDescriptor descriptor) -> TextureCube
    {
        return g_textureCube[NonUniformResourceIndex(descriptor.index)];
    }

    public func texture2DArray(SampledImageDescriptor descriptor) -> Texture2DArray
    {
        return g_texture2DArray[NonUniformResourceIndex(descriptor.index)];
    }

    public func storage2D(StorageImageDescriptor descriptor) -> RWTexture2D<float4>
    {
        return g_storage2D[NonUniformResourceIndex(descriptor.index)];
    }

    public func storage2DArray(StorageImageDescriptor descriptor) -> RWTexture2DArray<float4>
    {
        return g_storage2DArray[NonUniformResourceIndex(descriptor.index)];
    }

    public func sampler(SamplerDescriptor descriptor) -> SamplerState
    {
        return g_samplers[NonUniformResourceIndex(descriptor.index)];
    }

    /// An image plus the sampler to read it with: how assets::Texture reaches shaders, and the 8
    /// bytes a texture parameter of a material takes.
    public struct TextureDescriptor
    {
        public SampledImageDescriptor image;
        public SamplerDescriptor sampler;

        public func sample(float2 uv) -> float4
        {
            return texture2D(image).Sample(bindless::sampler(sampler), uv);
        }

        public func sampleLevel(float2 uv, float lod) -> float4
        {
            return texture2D(image).SampleLevel(bindless::sampler(sampler), uv, lod);
        }

        public func sampleCube(float3 direction) -> float4
        {
            return textureCube(image).Sample(bindless::sampler(sampler), direction);
        }

        public func sampleCubeLevel(float3 direction, float lod) -> float4
        {
            return textureCube(image).SampleLevel(bindless::sampler(sampler), direction, lod);
        }
    }
}
