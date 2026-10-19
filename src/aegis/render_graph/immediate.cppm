export module aegis.render_graph:immediate;
import std;
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
    if (auto flushed = device.flushUploads(); !flushed)
        return makeError(flushed.error(), std::format("{}: failed to flush uploads", name));

    auto pool = device.createCommandPool({ .name = name, .queueFamily = device.graphicsQueue().family() });
    if (!pool)
        return makeError(ErrorCode::SubmitFailed, pool.error(), std::format("{}: failed to create command pool", name));
    auto cmd = device.createCommandBuffer({ .name = name, .pool = *pool });
    if (!cmd)
        return makeError(ErrorCode::SubmitFailed, cmd.error(), std::format("{}: failed to create command buffer", name));

    cmd->begin(true);
    cmd->beginLabel(name);
    record(*cmd);
    cmd->endLabel();
    cmd->end();

    auto submitted = device.graphicsQueue().submit(*cmd);
    if (!submitted)
        return makeError(ErrorCode::SubmitFailed, submitted.error(), std::format("{}: failed to submit", name));
    if (!device.graphicsQueue().wait(*submitted))
        return makeError(ErrorCode::SubmitFailed, std::format("{}: failed to wait for completion", name));
    return {};
}
}
