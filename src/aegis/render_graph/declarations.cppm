export module aegis.render_graph:declarations;
import std;
import :error;
import :resource;
import :schedule;
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

/// @brief What the passes declare: the resources they create and every access they make, each stated once,
///        in the order the passes declared them. Needs no device, so a graph can be declared and planned
///        without one. Passes declare in their constructor; the declarations stay unchanged once compiled.
///
/// A pass's reads and writes follow from its declarations: an access is a write when its Usage writes
/// (detail::isWrite) or when it creates the resource.
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
        std::uint32_t pass; // The pass that made the declaration
    };

    /// @brief Starts the next pass: the declarations that follow belong to it. GraphBuilder::add calls this
    ///        before it constructs a pass.
    /// @return The pass's index, in the order the passes were added.
    auto beginPass(std::string_view name = {}) -> std::uint32_t;
    auto setPassName(std::uint32_t pass, std::string_view name) -> void;

    /// @brief Declares a texture the current pass creates. Names are unique among created resources.
    auto addTexture(std::string_view name, Usage usage, const TextureDesc& desc) -> ResourceId;
    /// @brief Declares a buffer the current pass creates. Names are unique among created resources.
    auto addBuffer(std::string_view name, Usage usage, const BufferDesc& desc) -> ResourceId;
    /// @brief Declares an access of the current pass to a resource another pass creates, found by name at
    ///        compile time.
    auto addReference(std::string_view name, Usage usage) -> ResourceId;

    [[nodiscard]] auto entries() const noexcept -> std::span<const Entry> { return m_entries; }
    [[nodiscard]] auto passCount() const noexcept -> std::uint32_t { return static_cast<std::uint32_t>(m_passNames.size()); }
    [[nodiscard]] auto passName(std::uint32_t pass) const -> std::string_view { return m_passNames[pass]; }

    /// @brief What the pass reads: its declarations that do not write, in declaration order.
    [[nodiscard]] auto reads(std::uint32_t pass) const -> std::vector<ResourceId>;
    /// @brief What the pass writes (or creates), in declaration order.
    [[nodiscard]] auto writes(std::uint32_t pass) const -> std::vector<ResourceId>;

    /// @brief Every declaration, in the order passes made them.
    [[nodiscard]] auto describe() const -> std::vector<ResourceDescription>;
    [[nodiscard]] auto name(ResourceId handle) const -> std::string_view { return m_entries[handle.index].name; }
    /// @brief The declaration creating the resource 'name' (not a reference to it), or none.
    [[nodiscard]] auto find(std::string_view name) const -> std::optional<ResourceId>;

private:
    auto add(std::string_view name, Usage usage, std::variant<TextureDesc, BufferDesc, Reference> info) -> ResourceId;

    std::vector<Entry> m_entries;
    std::vector<std::string> m_passNames;
};

/// @brief The outcome of compiling declarations, in terms of entries (indices into the declarations).
struct GraphPlan
{
    /// @brief Per entry: the entry that creates the resource (itself for a creator).
    std::vector<std::uint32_t> target;
    std::vector<detail::ResourceKind> kinds; // Per entry
    /// @brief Pass indices in execution order.
    std::vector<std::uint32_t> order;
    /// @brief Per entry: what every access to a created texture needs, plus TextureDesc::usage.
    std::vector<rhi::ImageUsage> imageUsage;
    /// @brief Per entry: what every access to a created buffer needs.
    std::vector<rhi::BufferUsage> bufferUsage;
    /// @brief Per entry: some pass writes a created buffer from the CPU.
    std::vector<bool> hostWritten;
    /// @brief Per entry of 'order': the barriers recorded before the pass.
    std::vector<std::vector<detail::Transition>> transitions;
};
}

export namespace aegis::render_graph::detail
{
/// @brief Compiles declarations into a plan without touching a device: resolves references, checks them, orders
///        the passes, merges the usage flags each resource needs and plans the barriers between passes.
/// @return InvalidGraph, naming the passes and resources involved, when
///         - two resources have the same name,
///         - a reference names nothing,
///         - an access uses a Usage its resource's kind does not support (a buffer usage on a texture or the
///           reverse),
///         - one pass accesses a texture in two layouts (its barriers are all recorded before it runs), or
///         - the passes depend on each other in a cycle.
[[nodiscard]] auto compilePlan(const ResourceDeclarations& declarations) -> std::expected<GraphPlan, Error>;

/// @brief The plan as text: the passes in execution order with their accesses and barriers, then the created
///        resources. For snapshot tests, so a change to scheduling or synchronization shows up as a diff.
[[nodiscard]] auto describePlan(const ResourceDeclarations& declarations, const GraphPlan& plan) -> std::string;
}
