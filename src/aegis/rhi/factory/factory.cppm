module;
#include <memory>

export module aegis.rhi.factory;
export import aegis.rhi;

#ifdef RHI_Vulkan
import aegis.rhi.vulkan;
#endif

export namespace aegis::rhi::factory
{
auto create(const Device::Desc& desc) -> std::unique_ptr<Device>
{
#ifdef RHI_Vulkan
    return std::make_unique<vulkan::Device>(desc);
#else
    static_assert(false, "No RHI Backend set");
#endif
}
}
