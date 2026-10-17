module aegis.render_graph;

import std;

namespace aegis::render_graph::detail
{
auto sortPasses(std::span<const PassDecl> passes, std::span<const EntryDecl> entries)
    -> std::expected<std::vector<std::uint32_t>, std::string>
{
    // dependencies[p] lists the passes p must run after.
    std::vector<std::vector<std::uint32_t>> dependencies(passes.size());
    std::unordered_map<std::uint32_t, std::uint32_t> producers; // Created entry -> latest producing pass

    for (std::uint32_t p = 0; p < passes.size(); ++p)
    {
        for (const auto write : passes[p].writes)
        {
            if (!entries[write].referenceTo)
                producers[write] = p;
        }
    }

    // Writes through a reference chain behind the previous producer, in declaration order.
    for (std::uint32_t p = 0; p < passes.size(); ++p)
    {
        for (const auto write : passes[p].writes)
        {
            const auto& target = entries[write].referenceTo;
            if (!target)
                continue;
            const auto producer = producers.find(*target);
            if (producer == producers.end() || producer->second == p)
                continue;
            dependencies[p].push_back(producer->second);
            producer->second = p;
        }
    }

    for (std::uint32_t p = 0; p < passes.size(); ++p)
    {
        for (const auto read : passes[p].reads)
        {
            const auto& target = entries[read].referenceTo;
            if (!target)
                continue;
            const auto producer = producers.find(*target);
            if (producer != producers.end() && producer->second != p)
                dependencies[p].push_back(producer->second);
        }
    }

    // Kahn's algorithm over the reversed edges, then reversed: dependencies come first.
    std::vector<std::uint32_t> dependents(passes.size(), 0);
    for (const auto& edges : dependencies)
    {
        for (const auto target : edges)
            ++dependents[target];
    }

    std::queue<std::uint32_t> ready;
    for (std::uint32_t p = 0; p < passes.size(); ++p)
    {
        if (dependents[p] == 0)
            ready.push(p);
    }

    std::vector<std::uint32_t> order;
    order.reserve(passes.size());
    while (!ready.empty())
    {
        const auto p = ready.front();
        ready.pop();
        order.push_back(p);
        for (const auto dependency : dependencies[p])
        {
            if (--dependents[dependency] == 0)
                ready.push(dependency);
        }
    }

    if (order.size() != passes.size())
        return std::unexpected{ std::string{ "The frame graph has a dependency cycle" } };

    std::ranges::reverse(order);
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
