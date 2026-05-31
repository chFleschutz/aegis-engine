module;
#include <string_view>

export module aegis.rhi:debug;
import vulkan_hpp;

export namespace aegis::rhi::debug
{
template<typename T>
concept IsVulkanHandle = requires
{
    { T::objectType } -> std::convertible_to<vk::ObjectType>;
    typename T::CType;
};

template<IsVulkanHandle T>
void setName(const vk::raii::Device& device, T handle, std::string_view name)
{
#ifndef NDEBUG
    const vk::DebugUtilsObjectNameInfoEXT info{
        .objectType = T::objectType,
        .objectHandle = reinterpret_cast<std::uint64_t>(static_cast<T::CType>(handle)),
        .pObjectName = name.data(),
    };
    std::ignore = device.setDebugUtilsObjectNameEXT(info);
#endif
}
}
