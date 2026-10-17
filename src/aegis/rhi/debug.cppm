export module aegis.rhi:debug;
import std;
import :context;
import vulkan;

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
    if constexpr (Context::enableValidation)
    {
        const std::string terminated{ name }; // Vulkan reads a null-terminated string
        const vk::DebugUtilsObjectNameInfoEXT info{
            .objectType = T::objectType,
            .objectHandle = reinterpret_cast<std::uint64_t>(static_cast<T::CType>(handle)),
            .pObjectName = terminated.c_str(),
        };
        std::ignore = device.setDebugUtilsObjectNameEXT(info);
    }
}
}
