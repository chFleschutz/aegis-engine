module;
#include <concepts>
#include <cstdint>
#include <expected>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

export module aegis.render_graph:frame_graph;
import :error;
import :render_pass;
import :resource_pool;
import :schedule;
import aegis.renderer;
import aegis.rhi;
import Aegis.Scene;

export namespace aegis::render_graph
{
/// @brief Orders render passes by their resource dependencies, creates the resources they declare,
///        and records each frame with the barriers between passes.
class FrameGraph
{
public:
    FrameGraph(rhi::Device& device, renderer::Renderer& renderer);
    FrameGraph(const FrameGraph&) = delete;
    FrameGraph(FrameGraph&&) = delete;
    ~FrameGraph() = default;

    auto operator=(const FrameGraph&) -> FrameGraph& = delete;
    auto operator=(FrameGraph&&) -> FrameGraph& = delete;

    /// @brief Adds a pass constructed as T(ResourcePool&, args...). Order only matters among passes
    ///        writing the same resource; compile() sorts the rest.
    template<typename T, typename... Args>
        requires std::derived_from<T, RenderPass> && std::constructible_from<T, ResourcePool&, Args...>
    auto add(Args&&... args) -> T&
    {
        auto pass = std::make_unique<T>(m_pool, std::forward<Args>(args)...);
        auto& ref = *pass;
        m_passes.push_back(std::move(pass));
        return ref;
    }

    /// @brief Resolves references, orders the passes, creates every resource, and plans the barriers.
    ///        Call once, after all passes were added.
    [[nodiscard]] auto compile() -> std::expected<void, Error>;

    /// @brief Call after renderer::Renderer::resize: graph textures keep their handles, but passes
    ///        may hold views of the replaced images.
    [[nodiscard]] auto resized() -> std::expected<void, Error>;

    /// @brief Tells every pass a scene was loaded (e.g. to upload static instance data).
    auto sceneInitialized(Aegis::Scene::Scene& scene) -> void;

    /// @brief Records every pass, in order, into frameInfo.cmd.
    auto execute(const FrameInfo& frameInfo) -> void;

    [[nodiscard]] auto pool() noexcept -> ResourcePool& { return m_pool; }

    /// @brief The passes in execution order (valid after compile).
    [[nodiscard]] auto passes() const -> std::vector<RenderPass*>;

private:
    auto recordTransitions(const rhi::CommandBuffer& cmd, const std::vector<detail::Transition>& transitions) const -> void;

    ResourcePool m_pool;
    std::vector<std::unique_ptr<RenderPass>> m_passes;          // In the order added
    std::vector<std::uint32_t> m_order;                         // Execution order, indices into m_passes
    std::vector<std::vector<detail::Transition>> m_transitions; // Per entry of m_order
    std::vector<detail::ResourceKind> m_kinds;                  // Per pool entry
};
}
