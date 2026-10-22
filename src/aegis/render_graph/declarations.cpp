module;
#include <cassert>

module aegis.render_graph;
import std;

namespace aegis::render_graph
{
auto ResourceDeclarations::beginPass(std::string_view name) -> std::uint32_t
{
    m_passNames.emplace_back(name);
    return static_cast<std::uint32_t>(m_passNames.size() - 1);
}

auto ResourceDeclarations::setPassName(std::uint32_t pass, std::string_view name) -> void
{
    m_passNames[pass] = name;
}

auto ResourceDeclarations::add(std::string_view name, Usage usage, std::variant<TextureDesc, BufferDesc, Reference> info)
    -> ResourceId
{
    assert(!m_passNames.empty() && "Declare resources after beginPass");
    m_entries.push_back({ std::string{ name }, usage, std::move(info), passCount() - 1 });
    return { static_cast<std::uint32_t>(m_entries.size() - 1) };
}

auto ResourceDeclarations::addTexture(std::string_view name, Usage usage, const TextureDesc& desc) -> ResourceId
{
    return add(name, usage, desc);
}

auto ResourceDeclarations::addBuffer(std::string_view name, Usage usage, const BufferDesc& desc) -> ResourceId
{
    return add(name, usage, desc);
}

auto ResourceDeclarations::addReference(std::string_view name, Usage usage) -> ResourceId
{
    return add(name, usage, Reference{});
}

namespace
{
auto isWriting(const ResourceDeclarations::Entry& entry) -> bool
{
    return detail::isWrite(entry.usage) || !std::holds_alternative<ResourceDeclarations::Reference>(entry.info);
}

auto declaredBy(const ResourceDeclarations& declarations, std::uint32_t pass, bool writes) -> std::vector<ResourceId>
{
    std::vector<ResourceId> handles;
    const auto entries = declarations.entries();
    for (std::uint32_t i = 0; i < entries.size(); ++i)
    {
        if (entries[i].pass == pass && isWriting(entries[i]) == writes)
            handles.push_back({ i });
    }
    return handles;
}
}

auto ResourceDeclarations::reads(std::uint32_t pass) const -> std::vector<ResourceId>
{
    return declaredBy(*this, pass, false);
}

auto ResourceDeclarations::writes(std::uint32_t pass) const -> std::vector<ResourceId>
{
    return declaredBy(*this, pass, true);
}

auto ResourceDeclarations::find(std::string_view name) const -> std::optional<ResourceId>
{
    const auto found = std::ranges::find_if(m_entries, [name](const Entry& entry) {
        return entry.name == name && !std::holds_alternative<Reference>(entry.info);
    });
    if (found == m_entries.end())
        return std::nullopt;
    return ResourceId{ static_cast<std::uint32_t>(found - m_entries.begin()) };
}

auto ResourceDeclarations::describe() const -> std::vector<ResourceDescription>
{
    std::vector<ResourceDescription> descriptions;
    descriptions.reserve(m_entries.size());
    for (const auto& entry : m_entries)
    {
        auto& description = descriptions.emplace_back(ResourceDescription{
            .name = entry.name,
            .kind = ResourceDescription::Kind::Reference,
            .usage = entry.usage,
        });
        if (const auto* texture = std::get_if<TextureDesc>(&entry.info))
        {
            description.kind = ResourceDescription::Kind::Texture;
            description.format = texture->format;
        }
        else if (std::holds_alternative<BufferDesc>(entry.info))
        {
            description.kind = ResourceDescription::Kind::Buffer;
        }
    }
    return descriptions;
}
}

namespace aegis::render_graph::detail
{
namespace
{
// Passes are named once constructed; a hand-built set of declarations may leave them unnamed.
auto passLabel(const ResourceDeclarations& declarations, std::uint32_t pass) -> std::string
{
    const auto name = declarations.passName(pass);
    return name.empty() ? std::format("#{}", pass) : std::string{ name };
}

auto invalid(std::string message) -> std::unexpected<Error>
{
    return makeError(ErrorCode::InvalidGraph, std::move(message));
}
}

auto compilePlan(const ResourceDeclarations& declarations) -> std::expected<GraphPlan, Error>
{
    const auto entries = declarations.entries();
    const auto count = static_cast<std::uint32_t>(entries.size());
    const auto isReference = [&](std::uint32_t i) { return std::holds_alternative<ResourceDeclarations::Reference>(entries[i].info); };
    const auto pass = [&](std::uint32_t entry) { return passLabel(declarations, entries[entry].pass); };

    GraphPlan plan;
    plan.target.assign(count, 0);

    // Created names are unique; every reference finds its creator.
    std::unordered_map<std::string_view, std::uint32_t> creators;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        if (isReference(i))
            continue;
        plan.target[i] = i;
        const auto [existing, inserted] = creators.try_emplace(entries[i].name, i);
        if (!inserted)
        {
            return invalid(std::format("Frame graph resource '{}' is created by both pass '{}' and pass '{}'",
                entries[i].name, pass(existing->second), pass(i)));
        }
    }
    for (std::uint32_t i = 0; i < count; ++i)
    {
        if (!isReference(i))
            continue;
        const auto creator = creators.find(entries[i].name);
        if (creator == creators.end())
        {
            return invalid(std::format("Frame graph resource '{}' is referenced by pass '{}' but never created",
                entries[i].name, pass(i)));
        }
        plan.target[i] = creator->second;
    }

    // An access must be one its resource supports.
    plan.kinds.resize(count);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const auto isBuffer = std::holds_alternative<BufferDesc>(entries[plan.target[i]].info);
        plan.kinds[i] = isBuffer ? ResourceKind::Buffer : ResourceKind::Image;
        if (isBuffer ? !supportsBuffer(entries[i].usage) : !supportsImage(entries[i].usage))
        {
            return invalid(std::format("Pass '{}' accesses {} '{}' with Usage::{}, which only {} support", pass(i),
                isBuffer ? "buffer" : "texture", entries[i].name, usageName(entries[i].usage), isBuffer ? "textures" : "buffers"));
        }
    }

    // Order the passes by what they read and write.
    std::vector<EntryDecl> entryDecls(count);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        if (isReference(i))
            entryDecls[i].referenceTo = plan.target[i];
    }
    std::vector<PassDecl> passDecls(declarations.passCount());
    for (std::uint32_t p = 0; p < passDecls.size(); ++p)
    {
        for (const auto read : declarations.reads(p))
            passDecls[p].reads.push_back(read.index);
        for (const auto write : declarations.writes(p))
            passDecls[p].writes.push_back(write.index);
    }
    auto order = sortPasses(passDecls, entryDecls);
    if (!order)
        return invalid(std::move(order.error()));
    plan.order = std::move(*order);

    // Every access contributes usage flags to the resource it touches.
    plan.imageUsage.assign(count, rhi::ImageUsage::None);
    plan.bufferUsage.assign(count, rhi::BufferUsage::None);
    plan.hostWritten.assign(count, false);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const auto target = plan.target[i];
        if (plan.kinds[i] == ResourceKind::Image)
            plan.imageUsage[target] = plan.imageUsage[target] | toImageUsage(entries[i].usage);
        else
            plan.bufferUsage[target] = plan.bufferUsage[target] | toBufferUsage(entries[i].usage);
        plan.hostWritten[target] = plan.hostWritten[target] || entries[i].usage == Usage::HostWrite;
    }
    for (std::uint32_t i = 0; i < count; ++i)
    {
        if (const auto* texture = std::get_if<TextureDesc>(&entries[i].info))
            plan.imageUsage[i] = plan.imageUsage[i] | texture->usage;
    }

    // A pass reads before it writes, and its barriers are all recorded before it runs.
    std::vector<std::vector<Access>> accesses;
    accesses.reserve(plan.order.size());
    for (const auto p : plan.order)
    {
        auto& passAccesses = accesses.emplace_back();
        std::unordered_map<std::uint32_t, std::uint32_t> imageAccess; // Target -> the first entry of this pass accessing it
        for (const auto& handles : { declarations.reads(p), declarations.writes(p) })
        {
            for (const auto handle : handles)
            {
                const auto target = plan.target[handle.index];
                passAccesses.push_back({ target, entries[handle.index].usage });
                if (plan.kinds[target] != ResourceKind::Image)
                    continue;

                const auto [first, inserted] = imageAccess.try_emplace(target, handle.index);
                if (!inserted && layoutClass(entries[first->second].usage) != layoutClass(entries[handle.index].usage))
                {
                    return invalid(std::format("Pass '{}' accesses texture '{}' as both {} and {}, which need different layouts",
                        passLabel(declarations, p), entries[target].name, usageName(entries[first->second].usage),
                        usageName(entries[handle.index].usage)));
                }
            }
        }
    }
    plan.transitions = planTransitions(accesses, plan.kinds);
    return plan;
}

auto describePlan(const ResourceDeclarations& declarations, const GraphPlan& plan) -> std::string
{
    const auto entries = declarations.entries();
    std::string text;
    for (std::size_t i = 0; i < plan.order.size(); ++i)
    {
        const auto p = plan.order[i];
        text += std::format("{}. {}\n", i + 1, passLabel(declarations, p));
        for (const auto& handles : { declarations.reads(p), declarations.writes(p) })
        {
            for (const auto handle : handles)
            {
                const auto& entry = entries[handle.index];
                const bool creates = !std::holds_alternative<ResourceDeclarations::Reference>(entry.info);
                text += std::format("    {} {} {}\n", creates ? "create" : "use   ", entry.name, usageName(entry.usage));
            }
        }
        for (const auto& transition : plan.transitions[i])
        {
            text += std::format("    barrier {}: {} -> {}\n", entries[transition.resource].name,
                transition.from ? usageName(*transition.from) : "undefined", usageName(transition.to));
        }
    }

    text += "resources\n";
    for (std::uint32_t i = 0; i < entries.size(); ++i)
    {
        if (plan.target[i] != i)
            continue;
        const auto& entry = entries[i];
        if (const auto* texture = std::get_if<TextureDesc>(&entry.info))
            text += std::format("    {}: texture {}\n", entry.name, rhi::formatName(texture->format));
        else
            text += std::format("    {}: buffer{}\n", entry.name, plan.hostWritten[i] ? ", host written" : "");
    }
    return text;
}
}
