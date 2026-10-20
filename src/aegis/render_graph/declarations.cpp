module aegis.render_graph;
import std;

namespace aegis::render_graph
{
auto ResourceDeclarations::addTexture(std::string_view name, Usage usage, const TextureDesc& desc) -> ResourceHandle
{
    m_entries.push_back({ std::string{ name }, usage, desc });
    return { static_cast<std::uint32_t>(m_entries.size() - 1) };
}

auto ResourceDeclarations::addBuffer(std::string_view name, Usage usage, const BufferDesc& desc) -> ResourceHandle
{
    m_entries.push_back({ std::string{ name }, usage, desc });
    return { static_cast<std::uint32_t>(m_entries.size() - 1) };
}

auto ResourceDeclarations::addReference(std::string_view name, Usage usage) -> ResourceHandle
{
    m_entries.push_back({ std::string{ name }, usage, Reference{} });
    return { static_cast<std::uint32_t>(m_entries.size() - 1) };
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
