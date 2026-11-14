module aegis.render_graph;

import std;

namespace aegis::render_graph::detail
{
auto sortPasses(std::span<const PassDecl> passes, std::span<const EntryDecl> entries)
    -> std::expected<std::vector<std::uint32_t>, std::string>
{
    // dependencies[p] lists the passes p must run after.
    std::vector<std::vector<std::uint32_t>> dependencies(passes.size());

    // Per created entry: its creator, and the state in declaration order (the pass that wrote it last
    // and the readers since that write). Passes declared before the creator still run after it.
    struct Chain
    {
        std::uint32_t creator{ 0 };
        std::uint32_t lastWriter{ 0 };
        std::vector<std::uint32_t> readers;
    };
    std::unordered_map<std::uint32_t, Chain> chains;
    for (std::uint32_t p = 0; p < passes.size(); ++p)
    {
        for (const auto write : passes[p].writes)
        {
            if (!entries[write].referenceTo)
                chains[write] = Chain{ .creator = p, .lastWriter = p };
        }
    }

    const auto dependOn = [&](std::uint32_t pass, std::uint32_t other) {
        if (pass != other)
            dependencies[pass].push_back(other);
    };

    // A reader runs after the writer before it in declaration order and before the next writer.
    for (std::uint32_t p = 0; p < passes.size(); ++p)
    {
        for (const auto read : passes[p].reads)
        {
            const auto& target = entries[read].referenceTo;
            if (!target)
                continue;
            const auto chain = chains.find(*target);
            if (chain == chains.end())
                continue;
            dependOn(p, chain->second.lastWriter);
            chain->second.readers.push_back(p);
        }

        for (const auto write : passes[p].writes)
        {
            const auto& target = entries[write].referenceTo;
            if (!target)
                continue;
            const auto chain = chains.find(*target);
            if (chain == chains.end())
                continue;
            dependOn(p, chain->second.lastWriter);
            for (const auto reader : chain->second.readers)
                dependOn(p, reader);
            chain->second.lastWriter = p;
            chain->second.readers.clear();
        }
    }

    // Kahn's algorithm, always taking the lowest pass index among the ready passes: independent
    // passes keep their add order.
    std::vector<std::uint32_t> pending(passes.size(), 0);
    std::vector<std::vector<std::uint32_t>> dependents(passes.size());
    for (std::uint32_t p = 0; p < passes.size(); ++p)
    {
        pending[p] = static_cast<std::uint32_t>(dependencies[p].size());
        for (const auto dependency : dependencies[p])
            dependents[dependency].push_back(p);
    }

    std::priority_queue<std::uint32_t, std::vector<std::uint32_t>, std::greater<>> ready;
    for (std::uint32_t p = 0; p < passes.size(); ++p)
    {
        if (pending[p] == 0)
            ready.push(p);
    }

    std::vector<std::uint32_t> order;
    order.reserve(passes.size());
    while (!ready.empty())
    {
        const auto p = ready.top();
        ready.pop();
        order.push_back(p);
        for (const auto dependent : dependents[p])
        {
            if (--pending[dependent] == 0)
                ready.push(dependent);
        }
    }

    if (order.size() != passes.size())
        return std::unexpected{ std::string{ "The frame graph has a dependency cycle" } };

    return order;
}

namespace
{
// Accesses are tracked per image: a history pair has two, the current one and the previous frame's.
auto slot(std::uint32_t resource, bool previous) -> std::size_t
{
    return std::size_t{ resource } * 2 + (previous ? 1 : 0);
}

// The last GPU access of each image or buffer in the frame.
auto lastAccesses(std::span<const std::vector<Access>> passes, std::size_t resourceCount) -> std::vector<std::optional<Usage>>
{
    std::vector<std::optional<Usage>> last(resourceCount * 2);
    for (const auto& accesses : passes)
    {
        for (const auto& access : accesses)
        {
            if (access.usage != Usage::HostWrite)
                last[slot(access.resource, access.previous)] = access.usage;
        }
    }
    return last;
}

auto needsBarrier(bool isImage, Usage from, Usage to) -> bool
{
    return (isImage && layoutClass(from) != layoutClass(to)) || isWrite(from) || isWrite(to);
}
}

auto planTransitions(std::span<const std::vector<Access>> passes, std::span<const PlannedResource> resources)
    -> std::vector<std::vector<Transition>>
{
    std::vector<std::vector<Transition>> transitions(passes.size());
    const auto lastInFrame = lastAccesses(passes, resources.size());

    // The barrier before the first access of the frame, if it needs one.
    const auto first = [&](const Access& access) -> std::optional<Transition> {
        const auto& [resource, usage, previous] = access;
        const auto& info = resources[resource];
        const bool isImage = info.kind == ResourceKind::Image;
        if (info.imported)
        {
            if (info.before ? !needsBarrier(isImage, *info.before, usage) : !isImage)
                return std::nullopt;
            return Transition{ .resource = resource, .from = info.before, .to = usage };
        }
        if (!isImage)
        {
            const auto last = *lastInFrame[slot(resource, false)];
            if (!isWrite(last) && !isWrite(usage))
                return std::nullopt;
            return Transition{ .resource = resource, .from = last, .to = usage, .crossFrame = true };
        }
        switch (info.lifetime)
        {
        case Lifetime::Frame:
            break;
        case Lifetime::Persistent:
            // Always planned, also between two reads in one layout: a fresh image comes from undefined.
            return Transition{ .resource = resource, .from = lastInFrame[slot(resource, false)], .to = usage, .crossFrame = true };
        case Lifetime::History:
            // The images swap: this one was the other of the pair a frame ago.
            if (const auto last = lastInFrame[slot(resource, !previous)])
                return Transition{ .resource = resource, .from = last, .to = usage, .previous = previous, .crossFrame = true };
            break;
        }
        return Transition{ .resource = resource, .from = std::nullopt, .to = usage, .previous = previous };
    };

    std::vector<std::optional<Usage>> current(resources.size() * 2);
    for (std::size_t p = 0; p < passes.size(); ++p)
    {
        for (const auto& access : passes[p])
        {
            if (access.usage == Usage::HostWrite)
                continue;

            auto& from = current[slot(access.resource, access.previous)];
            if (!from)
            {
                if (const auto transition = first(access))
                    transitions[p].push_back(*transition);
            }
            else if (needsBarrier(resources[access.resource].kind == ResourceKind::Image, *from, access.usage))
            {
                transitions[p].push_back({ .resource = access.resource, .from = from, .to = access.usage, .previous = access.previous });
            }
            from = access.usage;
        }
    }

    return transitions;
}

auto planEndOfFrame(std::span<const std::vector<Access>> passes, std::span<const PlannedResource> resources)
    -> std::vector<Transition>
{
    std::vector<Transition> transitions;
    const auto lastInFrame = lastAccesses(passes, resources.size());
    for (std::uint32_t resource = 0; resource < resources.size(); ++resource)
    {
        const auto& info = resources[resource];
        const auto last = lastInFrame[slot(resource, false)];
        if (!info.imported || !info.after || !last)
            continue;
        if (needsBarrier(info.kind == ResourceKind::Image, *last, *info.after))
            transitions.push_back({ .resource = resource, .from = last, .to = *info.after });
    }
    return transitions;
}
}
