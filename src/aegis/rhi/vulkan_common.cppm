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
    default: return vk::Format::eUndefined;
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
