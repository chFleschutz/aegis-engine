module;
#include <expected>
#include <format>
#include <string_view>

export module aegis.render_graph:immediate;
import :error;
import aegis.rhi;

export namespace aegis::render_graph
{
/// @brief Records 'record' into a one-time command buffer on the graphics queue, submits it and
///        waits for it. For setup work such as IBL precomputation, never per frame.
/// @note Flushes pending uploads first: setup work typically reads freshly uploaded resources, and
///       the same queue then orders the upload before it.
template<typename Record>
auto submitImmediate(rhi::Device& device, std::string_view name, Record&& record) -> std::expected<void, Error>
{
    std::ignore = device.flushUploads();

    auto pool = device.createCommandPool({ .name = name, .queueFamily = device.graphicsQueue().family() });
    if (!pool)
        return makeError(pool.error(), std::format("{}: failed to create command pool", name));
    auto cmd = device.createCommandBuffer({ .name = name, .pool = *pool });
    if (!cmd)
        return makeError(cmd.error(), std::format("{}: failed to create command buffer", name));

    cmd->begin(true);
    cmd->beginLabel(name);
    record(*cmd);
    cmd->endLabel();
    cmd->end();

    auto submitted = device.graphicsQueue().submit(*cmd);
    if (!submitted)
        return makeError(submitted.error(), std::format("{}: failed to submit", name));
    if (!device.graphicsQueue().wait(*submitted))
        return makeError(std::format("{}: failed to wait for completion", name));
    return {};
}
}
