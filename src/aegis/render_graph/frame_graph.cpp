module aegis.render_graph;
import std;
import aegis.renderer;
import aegis.rhi;
import aegis.scene;

namespace aegis::render_graph
{
FrameGraph::FrameGraph(rhi::Device& device, renderer::Renderer& renderer) :
    m_pool{ device, renderer, m_declarations }
{
}

auto FrameGraph::compile() -> std::expected<void, Error>
{
    if (m_compiled)
        return makeError(ErrorCode::InvalidGraph, "The frame graph is already compiled");

    auto plan = this->plan();
    if (!plan)
        return std::unexpected{ std::move(plan.error()) };
    m_order = plan->order;
    m_kinds = plan->kinds;

    if (auto created = m_pool.createResources(*plan); !created)
        return created;

    for (const auto index : m_order)
    {
        if (auto created = m_passes[index]->createResources(m_pool); !created)
        {
            auto error = created.error();
            error.message = std::format("{}: {}", m_passes[index]->name(), error.message);
            return std::unexpected{ std::move(error) };
        }
    }
    m_transitions = std::move(plan->transitions);

    const auto queryCount = static_cast<std::uint32_t>(2 * m_order.size());
    for (std::uint32_t i = 0; i < renderer::Renderer::maxFramesInFlight && queryCount > 0; ++i)
    {
        auto pool = m_pool.device().createQueryPool({ .name = "FrameGraphTimestamps", .count = queryCount });
        if (!pool)
            return makeError(ErrorCode::ResourceCreationFailed, "Failed to create the frame graph's timestamp queries", pool.error());
        m_timers.push_back({ .pool = std::move(*pool) });
    }
    m_compiled = true;
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
            .name = std::string{ m_passes[m_order[i]]->name() },
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

auto FrameGraph::sceneInitialized(aegis::scene::Scene& scene) -> void
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
        frameInfo.cmd.beginLabel(pass.name());
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

auto FrameGraph::describePasses() const -> std::vector<PassDescription>
{
    std::vector<PassDescription> ordered;
    ordered.reserve(m_order.size());
    for (const auto index : m_order)
    {
        ordered.push_back({
            .pass = m_passes[index].get(),
            .name = m_passes[index]->name(),
            .reads = m_declarations.reads(index),
            .writes = m_declarations.writes(index),
        });
    }
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
