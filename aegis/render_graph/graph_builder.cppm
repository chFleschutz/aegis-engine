export module aegis.render_graph:graph_builder;
import std;
import :declarations;
import :error;
import :render_pass;
import aegis.rhi;

export namespace aegis::render_graph
{
/// @brief The device-free half of a frame graph: the passes and the resources they declare. A graph can be
///        built and planned (plan()) without a device; FrameGraph adds the resources and the recording.
class GraphBuilder
{
public:
    GraphBuilder() = default;
    GraphBuilder(const GraphBuilder&) = delete;
    GraphBuilder(GraphBuilder&&) = delete;

    auto operator=(const GraphBuilder&) -> GraphBuilder& = delete;
    auto operator=(GraphBuilder&&) -> GraphBuilder& = delete;
    ~GraphBuilder() = default;

    /// @brief Adds a pass constructed as T(ResourceDeclarations&, args...). Order matters among passes accessing
    ///        the same resource (a reader runs before a writer declared after it); compile() sorts the
    ///        rest and independent passes keep their add order.
    template<typename T, typename... Args>
        requires std::derived_from<T, RenderPass> && std::constructible_from<T, ResourceDeclarations&, Args...>
    auto add(Args&&... args) -> T&
    {
        const auto index = m_declarations.beginPass();
        auto pass = std::make_unique<T>(m_declarations, std::forward<Args>(args)...);
        m_declarations.setPassName(index, pass->name());
        auto& ref = *pass;
        m_passes.push_back(std::move(pass));
        return ref;
    }

    /// @brief Starts a scope: the passes added until endScope create resources of their own, named after it
    ///        (ResourceDeclarations::beginScope), and its textures follow 'extent' (none: the output).
    auto beginScope(std::string_view name, std::optional<rhi::Extent2D> extent = std::nullopt) -> void
    {
        m_declarations.beginScope(name, extent);
    }
    /// @brief Ends the current scope.
    auto endScope() -> void { m_declarations.endScope(); }

    [[nodiscard]] auto declarations() const noexcept -> const ResourceDeclarations& { return m_declarations; }
    /// @brief The passes in the order added (FrameGraph::passes: in execution order, after compile).
    [[nodiscard]] auto addedPasses() const noexcept -> std::span<const std::unique_ptr<RenderPass>> { return m_passes; }

    /// @brief Compiles what the passes declared into a plan, without a device.
    [[nodiscard]] auto plan() const -> std::expected<GraphPlan, Error> { return detail::compilePlan(m_declarations); }

protected:
    ResourceDeclarations m_declarations;
    std::vector<std::unique_ptr<RenderPass>> m_passes; // In the order added
};
}
