module;
#include <expected>
#include <source_location>
#include <string_view>

export module aegis.rhi:error;
import :common;
import :vulkan;
import vulkan_hpp;

export namespace aegis::rhi
{
struct Error
{
    ErrorCode code;
#ifndef NDEBUG
    std::source_location origin = std::source_location::current();
#endif
};

auto vkError(vk::Result result, std::string_view operation) -> std::unexpected<Error>
{
    // TODO: Replace function
    return std::unexpected{ Error{ toRHI(result) } };
}
}
