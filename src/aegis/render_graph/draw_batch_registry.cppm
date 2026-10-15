module;
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

// Only the ECS headers: entt.hpp pulls in entt::meta, which crashes clang -O3 against libstdc++
#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>
#include <entt/entity/view.hpp>

export module aegis.render_graph:draw_batch_registry;
import aegis.assets;
import aegis.scene.registry;

export namespace aegis::render_graph
{
/// @brief The instances drawn with one material template, contiguous in the instance buffers.
struct DrawBatch
{
    std::uint32_t id;
    std::uint32_t firstInstance;
    std::uint32_t instanceCount;
    std::shared_ptr<const assets::MaterialTemplate> materialTemplate;
};

/// @brief Groups scene instances into draw batches by material template and keeps the counts the
///        GPU-driven path sizes its buffers by. Follows assets::Material components as they come and go.
class DrawBatchRegistry
{
public:
    static constexpr std::uint32_t maxDrawBatches{ 256 };

    DrawBatchRegistry() = default;
    DrawBatchRegistry(const DrawBatchRegistry&) = delete;
    DrawBatchRegistry(DrawBatchRegistry&&) = delete;
    ~DrawBatchRegistry();

    auto operator=(const DrawBatchRegistry&) -> DrawBatchRegistry& = delete;
    auto operator=(DrawBatchRegistry&&) -> DrawBatchRegistry& = delete;

    /// @brief Counts the registry's existing materials and follows its later changes.
    auto connect(Aegis::Scene::Registry& registry) -> void;
    auto disconnect() -> void;

    [[nodiscard]] auto batches() const noexcept -> const std::vector<DrawBatch>& { return m_batches; }
    [[nodiscard]] auto batchCount() const noexcept -> std::uint32_t { return static_cast<std::uint32_t>(m_batches.size()); }
    [[nodiscard]] auto instanceCount() const noexcept -> std::uint32_t { return m_staticCount + m_dynamicCount; }
    [[nodiscard]] auto staticInstanceCount() const noexcept -> std::uint32_t { return m_staticCount; }
    [[nodiscard]] auto dynamicInstanceCount() const noexcept -> std::uint32_t { return m_dynamicCount; }

    /// @return nullopt if no instance of 'materialTemplate' was ever added.
    [[nodiscard]] auto batchId(const assets::MaterialTemplate& materialTemplate) const -> std::optional<std::uint32_t>;

    /// @brief Counts one instance of 'materialTemplate', creating its batch on first use.
    auto addInstance(const std::shared_ptr<const assets::MaterialTemplate>& materialTemplate, bool dynamic) -> void;
    auto removeInstance(const assets::MaterialTemplate& materialTemplate, bool dynamic) -> void;
    /// @brief An instance moved between the static and dynamic sets (DynamicTag added or removed).
    auto setDynamic(bool dynamic) -> void;

private:
    auto onMaterialConstruct(entt::registry& registry, entt::entity entity) -> void;
    auto onMaterialDestroy(entt::registry& registry, entt::entity entity) -> void;
    auto onDynamicConstruct(entt::registry& registry, entt::entity entity) -> void;
    auto onDynamicDestroy(entt::registry& registry, entt::entity entity) -> void;
    auto updateOffsets() -> void;

    Aegis::Scene::Registry* m_registry{ nullptr };
    std::vector<DrawBatch> m_batches;
    std::unordered_map<const assets::MaterialTemplate*, std::uint32_t> m_batchIds;
    std::uint32_t m_staticCount{ 0 };
    std::uint32_t m_dynamicCount{ 0 };
};
}
