module;
#include <expected>
#include <source_location>
#include <string_view>

export module aegis.rhi:error;
import vulkan_hpp;

export namespace aegis::rhi
{
struct Error
{
    vk::Result result;
    std::string_view operation;
    std::source_location origin = std::source_location::current();
};

auto vkError(vk::Result result, std::string_view operation) -> std::unexpected<Error>
{
    return std::unexpected{ Error{ result, operation } };
}
}
