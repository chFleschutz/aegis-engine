export module aegis.render_graph:declarations;
import std;
import :resource;
import aegis.rhi;

export namespace aegis::render_graph
{
/// @brief One resource declaration as tools show it (the editor's renderer panel).
struct ResourceDescription
{
    enum class Kind
    {
        Texture,
        Buffer,
        Reference,
    };

    std::string_view name;
    Kind kind;
    Usage usage;
    std::optional<rhi::Format> format; // Textures only
};

/// @brief What the passes declare: the resources they create and the accesses they make, in the order
///        the passes declared them. Needs no device, so a graph can be declared and planned without one.
///        Passes declare in their constructor; the declarations stay unchanged once they are compiled.
class ResourceDeclarations
{
public:
    /// @brief An access to a resource another pass creates, found by name at compile time.
    struct Reference
    {
    };

    struct Entry
    {
        std::string name;
        Usage usage;
        std::variant<TextureDesc, BufferDesc, Reference> info;
    };

    /// @brief Declares a texture this pass creates. Names are unique among created resources.
    auto addTexture(std::string_view name, Usage usage, const TextureDesc& desc) -> ResourceHandle;
    /// @brief Declares a buffer this pass creates. Names are unique among created resources.
    auto addBuffer(std::string_view name, Usage usage, const BufferDesc& desc) -> ResourceHandle;
    /// @brief Declares an access to a resource another pass creates, found by name at compile time.
    auto addReference(std::string_view name, Usage usage) -> ResourceHandle;

    [[nodiscard]] auto entries() const noexcept -> std::span<const Entry> { return m_entries; }
    /// @brief Every declaration, in the order passes made them.
    [[nodiscard]] auto describe() const -> std::vector<ResourceDescription>;
    [[nodiscard]] auto name(ResourceHandle handle) const -> std::string_view { return m_entries[handle.index].name; }

private:
    std::vector<Entry> m_entries;
};
}
