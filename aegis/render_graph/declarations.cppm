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

/// @brief The name a resource 'name' created in the scope 'scope' has in the graph: "<scope>/<name>".
[[nodiscard]] auto scopedName(std::string_view scope, std::string_view name) -> std::string;

/// @brief What the passes declare: the resources they create and every access they make, each stated once,
///        in the order the passes declared them. Needs no device, so a graph can be declared and planned
///        without one. Passes declare in their constructor; the declarations stay unchanged once compiled.
///
/// A pass's reads and writes follow from its declarations: an access is a write when its Usage writes
/// (detail::isWrite) or when it creates the resource.
///
/// Passes declared inside a scope (beginScope) form a subgraph of their own, such as one view's scene passes: the
/// resources they create are named "<scope>/<name>" (scopedName), and their references find a resource of their
/// scope first, then one created outside any scope. So the same passes can be added once per scope, each scope
/// with its own resources, and still share the unscoped ones.
class ResourceDeclarations
{
public:
    /// @brief A scope: passes whose resources are named after it, and the extent its textures follow.
    struct Scope
    {
        std::string name;
        /// @brief The extent the scope's textures without one of their own follow (TextureDesc::extent); none: the
        ///        output's, like unscoped textures. FrameGraph::resized(scope, extent) changes it.
        std::optional<rhi::Extent2D> extent;
    };

    /// @brief An access to a resource another pass creates or imports, found by name at compile time.
    struct Reference
    {
        bool previous{ false }; // To the previous frame's image of a history pair
    };

    using Info = std::variant<TextureDesc, BufferDesc, Reference, ImportedTextureDesc, ImportedBufferDesc>;

    struct Entry
    {
        std::string name;
        Usage usage;
        Info info;
        std::uint32_t pass; // The pass that made the declaration
        /// @brief The scope the pass was declared in. A created resource's name already includes it; a reference
        ///        keeps the name it was declared with and is resolved in this scope first.
        std::optional<std::uint32_t> scope;
    };

    /// @brief Starts the next pass: the declarations that follow belong to it. GraphBuilder::add calls this
    ///        before it constructs a pass.
    /// @return The pass's index, in the order the passes were added.
    auto beginPass(std::string_view name = {}) -> std::uint32_t;
    /// @brief Names the pass; inside a scope, "<scope>/<name>".
    auto setPassName(std::uint32_t pass, std::string_view name) -> void;

    /// @brief Starts the scope 'name': the passes that follow, until endScope, belong to it. Scopes don't nest, and
    ///        each name is used once: not empty, without a '/'.
    /// @return The scope's index, in the order the scopes were begun.
    auto beginScope(std::string_view name, std::optional<rhi::Extent2D> extent = std::nullopt) -> std::uint32_t;
    /// @brief Ends the current scope: the passes that follow belong to none.
    auto endScope() -> void;
    [[nodiscard]] auto scopes() const noexcept -> std::span<const Scope> { return m_scopes; }
    /// @brief The index of the scope 'name', or none.
    [[nodiscard]] auto findScope(std::string_view name) const -> std::optional<std::uint32_t>;

    /// @brief Declares a texture the current pass creates. Names are unique among created resources (a scoped
    ///        resource's is its scoped name).
    auto addTexture(std::string_view name, Usage usage, const TextureDesc& desc) -> ResourceId;
    /// @brief Declares a buffer the current pass creates. Names are unique among created resources.
    auto addBuffer(std::string_view name, Usage usage, const BufferDesc& desc) -> ResourceId;
    /// @brief Declares a texture owned outside the graph, which the current pass accesses. The owner binds the
    ///        image with FrameGraph::bind; other passes reach it with addReference.
    auto importTexture(std::string_view name, Usage usage, const ImportedTextureDesc& desc) -> ResourceId;
    /// @brief Declares a buffer owned outside the graph, which the current pass accesses. The owner binds the
    ///        buffer with FrameGraph::bind; other passes reach it with addReference.
    auto importBuffer(std::string_view name, Usage usage, const ImportedBufferDesc& desc) -> ResourceId;
    /// @brief Declares an access of the current pass to a resource another pass creates or imports, found by
    ///        name at compile time: inside a scope, the scope's resource of that name if there is one, else the
    ///        unscoped one.
    auto addReference(std::string_view name, Usage usage) -> ResourceId;
    /// @brief Declares a read of the previous frame's image of the history pair 'name' (a texture created with
    ///        Lifetime::History). The returned id is that image in the ResourcePool, a different one each
    ///        frame. It orders the pass against no other: the pass creating the pair may read it too.
    auto addHistoryReference(std::string_view name, Usage usage) -> ResourceId;

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
    /// @brief The declaration creating or importing the resource 'name' (not a reference to it), or none. A scoped
    ///        resource goes by its scoped name (scopedName).
    [[nodiscard]] auto find(std::string_view name) const -> std::optional<ResourceId>;

private:
    auto add(std::string_view name, Usage usage, Info info) -> ResourceId;

    std::vector<Entry> m_entries;
    std::vector<std::string> m_passNames;
    std::vector<std::optional<std::uint32_t>> m_passScopes; // Per pass
    std::vector<Scope> m_scopes;
    std::optional<std::uint32_t> m_scope; // The current scope
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
    /// @brief Per entry: the access is to the previous frame's image of a history pair.
    std::vector<bool> previous;
    /// @brief Per entry of 'order': the barriers recorded before the pass.
    std::vector<std::vector<detail::Transition>> transitions;
    /// @brief The barriers recorded after the last pass, which leave imported resources as declared.
    std::vector<detail::Transition> endOfFrame;
};
}

export namespace aegis::render_graph::detail
{
/// @brief Compiles declarations into a plan without touching a device: resolves references, checks them, orders
///        the passes, merges the usage flags each resource needs and plans the barriers between passes.
/// @return InvalidGraph, naming the passes and resources involved, when
///         - two resources have the same name,
///         - a reference names nothing (in its scope or outside any),
///         - a history reference names a texture that is no history pair, or writes,
///         - an access uses a Usage its resource's kind does not support (a buffer usage on a texture or the
///           reverse),
///         - one pass accesses a texture in two layouts (its barriers are all recorded before it runs), or
///         - the passes depend on each other in a cycle.
[[nodiscard]] auto compilePlan(const ResourceDeclarations& declarations) -> std::expected<GraphPlan, Error>;

/// @brief The plan as text: the passes in execution order with their accesses and barriers, then the created
///        resources. Accesses and barriers that reach into the previous frame are marked. For snapshot tests,
///        so a change to scheduling or synchronization shows up as a diff.
[[nodiscard]] auto describePlan(const ResourceDeclarations& declarations, const GraphPlan& plan) -> std::string;
}
