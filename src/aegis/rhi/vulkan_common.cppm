module;
#include <cassert>

export module aegis.rhi:vulkan_common;
import :common;
import vulkan_hpp;

export namespace aegis::rhi
{
auto toVk(Format format) -> vk::Format
{
    switch (format)
    {
    case Format::Unknown: return vk::Format::eUndefined;
    case Format::R8_UNORM: return vk::Format::eR8Unorm;
    case Format::RG8_UNORM: return vk::Format::eR8G8Unorm;
    case Format::RGBA8_UNORM: return vk::Format::eR8G8B8A8Unorm;
    case Format::RGBA8_SRGB: return vk::Format::eR8G8B8A8Srgb;
    case Format::BGRA8_UNORM: return vk::Format::eB8G8R8A8Unorm;
    case Format::RGB10A2_UNORM: return vk::Format::eA2R10G10B10UnormPack32;
    case Format::B10G11R11_UFLOAT: return vk::Format::eB10G11R11UfloatPack32;
    case Format::R16_UNORM: return vk::Format::eR16Unorm;
    case Format::RG16_UNORM: return vk::Format::eR16G16Unorm;
    case Format::RGBA16_UNORM: return vk::Format::eR16G16B16A16Unorm;
    case Format::RGBA16_SFLOAT: return vk::Format::eR16G16B16A16Sfloat;
    case Format::R32_SFLOAT: return vk::Format::eR32Sfloat;
    case Format::RG32_SFLOAT: return vk::Format::eR32G32Sfloat;
    case Format::RGB32_SFLOAT: return vk::Format::eR32G32B32Sfloat;
    case Format::RGBA32_SFLOAT: return vk::Format::eR32G32B32A32Sfloat;
    case Format::D32_SFLOAT: return vk::Format::eD32Sfloat;
    case Format::D24_UNORM_S8_UINT: return vk::Format::eD24UnormS8Uint;
    case Format::D32_SFLOAT_S8_UINT: return vk::Format::eD32SfloatS8Uint;
    default:
    {
        assert(false && "Unknown format");
        return vk::Format::eUndefined;
    }
    }
}

auto fromVk(vk::Format format) -> Format
{
    switch (format)
    {
    case vk::Format::eUndefined: return Format::Unknown;
    case vk::Format::eR8Unorm: return Format::R8_UNORM;
    case vk::Format::eR8G8Unorm: return Format::RG8_UNORM;
    case vk::Format::eR8G8B8A8Unorm: return Format::RGBA8_UNORM;
    case vk::Format::eR8G8B8A8Srgb: return Format::RGBA8_SRGB;
    case vk::Format::eB8G8R8A8Unorm: return Format::BGRA8_UNORM;
    case vk::Format::eA2R10G10B10UnormPack32: return Format::RGB10A2_UNORM;
    case vk::Format::eB10G11R11UfloatPack32: return Format::B10G11R11_UFLOAT;
    case vk::Format::eR16Unorm: return Format::R16_UNORM;
    case vk::Format::eR16G16Unorm: return Format::RG16_UNORM;
    case vk::Format::eR16G16B16A16Unorm: return Format::RGBA16_UNORM;
    case vk::Format::eR16G16B16A16Sfloat: return Format::RGBA16_SFLOAT;
    case vk::Format::eR32Sfloat: return Format::R32_SFLOAT;
    case vk::Format::eR32G32Sfloat: return Format::RG32_SFLOAT;
    case vk::Format::eR32G32B32Sfloat: return Format::RGB32_SFLOAT;
    case vk::Format::eR32G32B32A32Sfloat: return Format::RGBA32_SFLOAT;
    case vk::Format::eD32Sfloat: return Format::D32_SFLOAT;
    case vk::Format::eD24UnormS8Uint: return Format::D24_UNORM_S8_UINT;
    case vk::Format::eD32SfloatS8Uint: return Format::D32_SFLOAT_S8_UINT;
    default:
    {
        assert(false && "Unsupported vulkan format");
        return Format::Unknown;
    }
    }
}

auto toVkType(ShaderStage stage) -> vk::ShaderStageFlagBits
{
    switch (stage)
    {
    case ShaderStage::Vertex: return vk::ShaderStageFlagBits::eVertex;
    case ShaderStage::TessellationControl: return vk::ShaderStageFlagBits::eTessellationControl;
    case ShaderStage::TessellationEvaluation:
        return vk::ShaderStageFlagBits::eTessellationEvaluation;
    case ShaderStage::Geometry: return vk::ShaderStageFlagBits::eGeometry;
    case ShaderStage::Fragment: return vk::ShaderStageFlagBits::eFragment;
    case ShaderStage::Compute: return vk::ShaderStageFlagBits::eCompute;
    case ShaderStage::Task: return vk::ShaderStageFlagBits::eTaskEXT;
    case ShaderStage::Mesh: return vk::ShaderStageFlagBits::eMeshEXT;
    default:
    {
        assert(false && "Unknown pipeline stage");
        return vk::ShaderStageFlagBits::eAll;
    }
    }
}

}
