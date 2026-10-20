export module aegis.render_graph:graph_builder;
import std;
import :declarations;
import :error;
import :render_pass;

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

    [[nodiscard]] auto declarations() const noexcept -> const ResourceDeclarations& { return m_declarations; }

    /// @brief Compiles what the passes declared into a plan, without a device.
    [[nodiscard]] auto plan() const -> std::expected<GraphPlan, Error> { return detail::compilePlan(m_declarations); }

protected:
    ResourceDeclarations m_declarations;
    std::vector<std::unique_ptr<RenderPass>> m_passes; // In the order added
};
}
