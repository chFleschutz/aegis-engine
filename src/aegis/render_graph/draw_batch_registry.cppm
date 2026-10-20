export module aegis.render_graph:draw_batch_registry;
import std;
import aegis.assets;
import aegis.world.render_components;
import aegis.world.registry;
import :error;

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
///        GPU-driven path sizes its buffers by. Follows aegis::Material components as they come and go.
///        It also reports every change to the static set (instances and meshes coming and going, entities
///        moving between static and dynamic) to the scene registry's static version, and marks new static
///        entities dirty so the TransformSystem computes their global transforms.
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

    /// @brief Counts the registry's existing materials and follows its later changes. Forgets everything
    ///        counted before, so a second connect never double-counts.
    auto connect(aegis::world::Registry& registry) -> void;
    auto disconnect() -> void;

    [[nodiscard]] auto batches() const noexcept -> const std::vector<DrawBatch>& { return m_batches; }
    [[nodiscard]] auto batchCount() const noexcept -> std::uint32_t { return static_cast<std::uint32_t>(m_batches.size()); }
    [[nodiscard]] auto instanceCount() const noexcept -> std::uint32_t { return m_staticCount + m_dynamicCount; }
    [[nodiscard]] auto staticInstanceCount() const noexcept -> std::uint32_t { return m_staticCount; }
    [[nodiscard]] auto dynamicInstanceCount() const noexcept -> std::uint32_t { return m_dynamicCount; }

    /// @return nullopt if no instance of 'materialTemplate' was ever added.
    [[nodiscard]] auto batchId(const assets::MaterialTemplate& materialTemplate) const -> std::optional<std::uint32_t>;

    /// @brief Counts the instance of 'entity', creating the batch of its template on first use.
    ///        An entity that is already tracked is untracked first, so it moves instead of counting twice.
    /// @return An error if a new template would exceed maxDrawBatches; nothing is counted then.
    [[nodiscard]] auto track(entt::entity entity, const std::shared_ptr<const assets::MaterialTemplate>& materialTemplate,
        bool dynamic) -> std::expected<void, Error>;
    /// @brief Stops counting the instance of 'entity'. Does nothing for an entity that is not tracked.
    auto untrack(entt::entity entity) -> void;
    /// @brief The instance of 'entity' moved between the static and dynamic sets (DynamicTag added or
    ///        removed). Does nothing for an entity that is not tracked.
    auto setDynamic(entt::entity entity, bool dynamic) -> void;

private:
    auto onMaterialConstruct(entt::registry& registry, entt::entity entity) -> void;
    auto onMaterialDestroy(entt::registry& registry, entt::entity entity) -> void;
    auto onDynamicConstruct(entt::registry& registry, entt::entity entity) -> void;
    auto onDynamicDestroy(entt::registry& registry, entt::entity entity) -> void;
    auto onMeshConstruct(entt::registry& registry, entt::entity entity) -> void;
    auto onMeshDestroy(entt::registry& registry, entt::entity entity) -> void;
    auto markStatic(entt::registry& registry, entt::entity entity) -> void;
    auto trackMaterial(entt::entity entity, const aegis::Material& material, bool dynamic) -> void;
    auto updateOffsets() -> void;
    auto clear() -> void;

    struct Tracked
    {
        const assets::MaterialTemplate* materialTemplate;
        bool dynamic;
    };

    aegis::world::Registry* m_registry{ nullptr };
    std::vector<DrawBatch> m_batches;
    std::unordered_map<const assets::MaterialTemplate*, std::uint32_t> m_batchIds;
    std::unordered_map<entt::entity, Tracked> m_tracked;
    std::uint32_t m_staticCount{ 0 };
    std::uint32_t m_dynamicCount{ 0 };
};
}
