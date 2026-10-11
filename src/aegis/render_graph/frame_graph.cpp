module;
#include <chrono>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <variant>
#include <vector>

module aegis.render_graph;
import aegis.renderer;
import aegis.rhi;
import Aegis.Scene;

namespace aegis::render_graph
{
FrameGraph::FrameGraph(rhi::Device& device, renderer::Renderer& renderer) :
    m_pool{ device, renderer }
{
}

auto FrameGraph::compile() -> std::expected<void, Error>
{
    if (auto resolved = m_pool.resolveReferences(); !resolved)
        return resolved;

    // Pure scheduling inputs: which entries each pass touches, and what each entry is.
    const auto& entries = m_pool.m_entries;
    std::vector<detail::EntryDecl> entryDecls(entries.size());
    m_kinds.assign(entries.size(), detail::ResourceKind::Image);
    for (std::uint32_t i = 0; i < entries.size(); ++i)
    {
        if (const auto* reference = std::get_if<ResourcePool::Reference>(&entries[i].info))
            entryDecls[i].referenceTo = reference->target;
        else if (std::holds_alternative<BufferDesc>(entries[i].info))
            m_kinds[i] = detail::ResourceKind::Buffer;
    }

    std::vector<RenderPass::Info> infos;
    std::vector<detail::PassDecl> passDecls;
    infos.reserve(m_passes.size());
    for (const auto& pass : m_passes)
    {
        auto& info = infos.emplace_back(pass->info());
        auto& decl = passDecls.emplace_back();
        for (const auto read : info.reads)
            decl.reads.push_back(read.index);
        for (const auto write : info.writes)
            decl.writes.push_back(write.index);
    }

    auto order = detail::sortPasses(passDecls, entryDecls);
    if (!order)
        return makeError(order.error());
    m_order = std::move(*order);

    if (auto created = m_pool.createResources(); !created)
        return created;

    for (const auto index : m_order)
    {
        if (auto created = m_passes[index]->createResources(m_pool); !created)
        {
            auto error = created.error();
            error.message = std::format("{}: {}", infos[index].name, error.message);
            return std::unexpected{ std::move(error) };
        }
    }

    // A pass reads before it writes.
    std::vector<std::vector<detail::Access>> accesses;
    accesses.reserve(m_order.size());
    for (const auto index : m_order)
    {
        auto& passAccesses = accesses.emplace_back();
        for (const auto& handles : { infos[index].reads, infos[index].writes })
        {
            for (const auto handle : handles)
                passAccesses.push_back({ m_pool.actual(handle), entries[handle.index].usage });
        }
    }
    m_transitions = detail::planTransitions(accesses, m_kinds);

    const auto queryCount = static_cast<std::uint32_t>(2 * m_order.size());
    for (std::uint32_t i = 0; i < renderer::Renderer::maxFramesInFlight && queryCount > 0; ++i)
    {
        auto pool = m_pool.device().createQueryPool({ .name = "FrameGraphTimestamps", .count = queryCount });
        if (!pool)
            return makeError(pool.error(), "Failed to create the frame graph's timestamp queries");
        m_timers.push_back({ .pool = std::move(*pool) });
    }
    return {};
}

auto FrameGraph::readTimings(FrameTimer& timer) -> void
{
    timer.pending = false;
    auto timestamps = timer.pool.readTimestamps(0, timer.pool.count());
    if (!timestamps)
        return; // Not ready: keep the previous frame's timings

    m_timings.resize(m_order.size());
    for (std::size_t i = 0; i < m_order.size(); ++i)
    {
        m_timings[i] = {
            .name = m_passes[m_order[i]]->info().name,
            .gpuMs = timer.pool.elapsedMs((*timestamps)[2 * i], (*timestamps)[2 * i + 1]),
            .cpuMs = timer.cpuMs[i],
        };
    }
}

auto FrameGraph::resized() -> std::expected<void, Error>
{
    for (const auto index : m_order)
    {
        if (auto result = m_passes[index]->resized(m_pool); !result)
            return result;
    }
    return {};
}

auto FrameGraph::sceneInitialized(Aegis::Scene::Scene& scene) -> void
{
    for (const auto index : m_order)
        m_passes[index]->sceneInitialized(m_pool, scene);
}

auto FrameGraph::execute(const FrameInfo& frameInfo) -> void
{
    // The renderer waited for the frame that last used this slot, so its timestamps are final.
    auto* timer = m_timers.empty() ? nullptr : &m_timers[frameInfo.frameIndex];
    if (timer)
    {
        if (timer->pending)
            readTimings(*timer);
        frameInfo.cmd.resetQueries(timer->pool, 0, timer->pool.count());
        timer->cpuMs.assign(m_order.size(), 0.0);
    }

    for (std::size_t i = 0; i < m_order.size(); ++i)
    {
        const auto cpuStart = std::chrono::steady_clock::now();
        auto& pass = *m_passes[m_order[i]];
        frameInfo.cmd.beginLabel(pass.info().name);
        if (timer)
            frameInfo.cmd.writeTimestamp(timer->pool, static_cast<std::uint32_t>(2 * i), rhi::TimestampStage::TopOfPipe);

        recordTransitions(frameInfo.cmd, m_transitions[i]);
        pass.execute(m_pool, frameInfo);

        if (timer)
        {
            frameInfo.cmd.writeTimestamp(timer->pool, static_cast<std::uint32_t>(2 * i + 1),
                rhi::TimestampStage::AllCommands);
            timer->cpuMs[i] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cpuStart).count();
        }
        frameInfo.cmd.endLabel();
    }
    if (timer)
        timer->pending = true;
}

auto FrameGraph::passes() const -> std::vector<RenderPass*>
{
    std::vector<RenderPass*> ordered;
    ordered.reserve(m_order.size());
    for (const auto index : m_order)
        ordered.push_back(m_passes[index].get());
    return ordered;
}

auto FrameGraph::recordTransitions(const rhi::CommandBuffer& cmd,
    const std::vector<detail::Transition>& transitions) const -> void
{
    for (const auto& [resource, from, to] : transitions)
    {
        const auto fromState = from ? detail::toResourceState(*from) : rhi::ResourceState::Unknown;
        const auto toState = detail::toResourceState(to);
        if (m_kinds[resource] == detail::ResourceKind::Image)
            cmd.transitionImageLayout(m_pool.texture({ resource }).image, fromState, toState);
        else
            cmd.memoryBarrier(fromState, toState);
    }
}
}
