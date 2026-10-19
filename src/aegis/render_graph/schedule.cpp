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

auto planTransitions(std::span<const std::vector<Access>> passes, std::span<const ResourceKind> resources)
    -> std::vector<std::vector<Transition>>
{
    std::vector<std::vector<Transition>> transitions(passes.size());

    // The last GPU access of each resource in the frame: where a buffer's first access syncs from.
    std::vector<std::optional<Usage>> lastInFrame(resources.size());
    for (const auto& accesses : passes)
    {
        for (const auto& access : accesses)
        {
            if (access.usage != Usage::HostWrite)
                lastInFrame[access.resource] = access.usage;
        }
    }

    std::vector<std::optional<Usage>> previous(resources.size());
    std::vector<bool> seen(resources.size(), false);
    for (std::size_t p = 0; p < passes.size(); ++p)
    {
        for (const auto& [resource, usage] : passes[p])
        {
            if (usage == Usage::HostWrite)
                continue;

            const bool isImage = resources[resource] == ResourceKind::Image;
            if (!seen[resource])
            {
                seen[resource] = true;
                previous[resource] = usage;
                if (isImage)
                {
                    transitions[p].push_back({ resource, std::nullopt, usage });
                }
                else
                {
                    const auto last = *lastInFrame[resource];
                    if (isWrite(last) || isWrite(usage))
                        transitions[p].push_back({ resource, last, usage });
                }
                continue;
            }

            const auto from = *previous[resource];
            const bool layoutChanges = isImage && layoutClass(from) != layoutClass(usage);
            if (layoutChanges || isWrite(from) || isWrite(usage))
                transitions[p].push_back({ resource, from, usage });
            previous[resource] = usage;
        }
    }

    return transitions;
}
}
