export module aegis.render_graph:instance_store;
import std;
import aegis.assets;
import aegis.math;
import :error;

export namespace aegis::render_graph::detail
{
/// @brief The top three rows of 'model', each with its translation in .w: how shaders, and InstanceStore,
///        hold a model matrix.
[[nodiscard]] inline auto toModelRows(const glm::mat4& model) -> glm::mat3x4
{
    return glm::mat3x4{ glm::rowMajor4(model) };
}

/// @brief Whether two model matrices (Instance::model) are equal, element by element.
[[nodiscard]] constexpr auto sameModel(const glm::mat3x4& a, const glm::mat3x4& b) noexcept -> bool
{
    for (int column = 0; column < 3; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            if (a[column][row] != b[column][row])
                return false;
        }
    }
    return true;
}
}

export namespace aegis::render_graph
{
/// @brief The instances drawn with one material template. Culling appends a batch's visible instances
///        from firstInstance on, so the batches tile [0, instance count) in id order.
struct DrawBatch
{
    std::uint32_t id;
    std::uint32_t firstInstance;
    std::uint32_t instanceCount;
    std::shared_ptr<const assets::MaterialTemplate> materialTemplate;
};

/// @brief Names one instance of an InstanceStore from add until remove. The instance's position in the
///        store's arrays changes as others come and go; its id does not. A removed id may be reused.
struct InstanceId
{
    std::uint32_t index;

    auto operator==(const InstanceId&) const -> bool = default;
};

/// @brief The half-open range [begin, end) of array positions that changed.
struct DirtyRange
{
    std::uint32_t begin{ 0 };
    std::uint32_t end{ 0 };

    /// @brief Every position: what a consumer that has written nothing yet must write.
    [[nodiscard]] static constexpr auto all() noexcept -> DirtyRange { return { 0, std::numeric_limits<std::uint32_t>::max() }; }

    [[nodiscard]] constexpr auto empty() const noexcept -> bool { return begin >= end; }
    [[nodiscard]] constexpr auto size() const noexcept -> std::uint32_t { return empty() ? 0 : end - begin; }
    /// @brief The smallest range covering both.
    [[nodiscard]] constexpr auto merged(DirtyRange other) const noexcept -> DirtyRange
    {
        if (empty())
            return other;
        if (other.empty())
            return *this;
        return { std::min(begin, other.begin), std::max(end, other.end) };
    }
    /// @brief The part below 'count'.
    [[nodiscard]] constexpr auto clipped(std::uint32_t count) const noexcept -> DirtyRange
    {
        return { std::min(begin, count), std::min(end, count) };
    }

    auto operator==(const DirtyRange&) const -> bool = default;
};

/// @brief What one instance draws.
struct Instance
{
    /// @brief The top three rows of the model matrix, as shaders read it (detail::toModelRows).
    glm::mat3x4 model{ 1.0f };
    std::shared_ptr<const assets::StaticMesh> mesh;
    std::shared_ptr<assets::Material> material;
    /// @brief The id of the instance's draw batch, whose template is the material's. Set by the store; add
    ///        and update ignore it.
    std::uint32_t drawBatch{ 0 };
};

/// @brief The instances both geometry paths draw, in two dense arrays: the static set and the dynamic set.
///        Each array is slot-allocated: a new instance goes to the end, a removed one is replaced by the last.
///        Instances are grouped into draw batches by material template, and each array records the range
///        of positions that changed since the last clearDirty(), so the GPU-driven path rewrites only those.
///        A plain value, filled by the app from the world (aegis::InstanceBridge) and read by the passes
///        through FrameScene::instances.
class InstanceStore
{
public:
    static constexpr std::uint32_t maxDrawBatches{ 256 };

    InstanceStore() = default;
    InstanceStore(const InstanceStore&) = delete;
    InstanceStore(InstanceStore&&) = delete;
    ~InstanceStore() = default;

    auto operator=(const InstanceStore&) -> InstanceStore& = delete;
    auto operator=(InstanceStore&&) -> InstanceStore& = delete;

    /// @brief Adds an instance to the static or dynamic set, in the batch of 'materialTemplate' (its material's
    ///        template, never null), which is created on first use.
    /// @return An error if a new template would exceed maxDrawBatches; nothing is added then.
    [[nodiscard]] auto add(Instance instance, const std::shared_ptr<const assets::MaterialTemplate>& materialTemplate,
        bool dynamic) -> std::expected<InstanceId, Error>;
    /// @brief Removes the instance. 'id' must name one.
    auto remove(InstanceId id) -> void;
    /// @brief Replaces what the instance draws, moving it to the batch of 'materialTemplate' if that is another
    ///        one. Marks it dirty only if something changed.
    /// @return An error if a new template would exceed maxDrawBatches; the instance is unchanged then.
    [[nodiscard]] auto update(InstanceId id, Instance instance,
        const std::shared_ptr<const assets::MaterialTemplate>& materialTemplate) -> std::expected<void, Error>;
    /// @brief Moves the instance between the static and dynamic sets. Does nothing if it is in 'dynamic' already.
    auto setDynamic(InstanceId id, bool dynamic) -> void;
    /// @brief Removes every instance and batch.
    auto clear() -> void;

    [[nodiscard]] auto contains(InstanceId id) const noexcept -> bool;
    [[nodiscard]] auto get(InstanceId id) const -> const Instance&;
    [[nodiscard]] auto isDynamic(InstanceId id) const -> bool;

    [[nodiscard]] auto staticInstances() const noexcept -> std::span<const Instance> { return m_static.instances; }
    [[nodiscard]] auto dynamicInstances() const noexcept -> std::span<const Instance> { return m_dynamic.instances; }
    /// @brief The positions of each set that changed since the last clearDirty(), within its current size.
    [[nodiscard]] auto staticDirty() const noexcept -> DirtyRange { return m_static.dirty.clipped(staticInstanceCount()); }
    [[nodiscard]] auto dynamicDirty() const noexcept -> DirtyRange { return m_dynamic.dirty.clipped(dynamicInstanceCount()); }
    /// @brief Forgets the changes, once the frame that read them has been recorded.
    auto clearDirty() noexcept -> void;

    /// @brief Every batch ever created, by id; a batch whose instances are all gone stays, with a count of 0.
    [[nodiscard]] auto batches() const noexcept -> const std::vector<DrawBatch>& { return m_batches; }
    [[nodiscard]] auto batchCount() const noexcept -> std::uint32_t { return static_cast<std::uint32_t>(m_batches.size()); }
    /// @return nullopt if no instance of 'materialTemplate' was ever added.
    [[nodiscard]] auto batchId(const assets::MaterialTemplate& materialTemplate) const -> std::optional<std::uint32_t>;

    [[nodiscard]] auto instanceCount() const noexcept -> std::uint32_t { return staticInstanceCount() + dynamicInstanceCount(); }
    [[nodiscard]] auto staticInstanceCount() const noexcept -> std::uint32_t { return static_cast<std::uint32_t>(m_static.instances.size()); }
    [[nodiscard]] auto dynamicInstanceCount() const noexcept -> std::uint32_t { return static_cast<std::uint32_t>(m_dynamic.instances.size()); }

    /// @brief The distinct materials of the instances, each once, for the passes to update per frame.
    [[nodiscard]] auto materials() const noexcept -> std::span<const std::shared_ptr<assets::Material>> { return m_materials; }

private:
    struct Set
    {
        std::vector<Instance> instances;
        std::vector<InstanceId> ids; // Per position: the id of the instance there
        DirtyRange dirty;
    };

    struct Location
    {
        bool dynamic;
        std::uint32_t position;
    };

    struct MaterialUse
    {
        std::uint32_t index; // In m_materials
        std::uint32_t count; // Instances using it
    };

    [[nodiscard]] auto set(bool dynamic) noexcept -> Set& { return dynamic ? m_dynamic : m_static; }
    [[nodiscard]] auto location(InstanceId id) const -> const Location&;
    /// @brief The id of the batch of 'materialTemplate', created if it is new.
    [[nodiscard]] auto acquireBatch(const std::shared_ptr<const assets::MaterialTemplate>& materialTemplate)
        -> std::expected<std::uint32_t, Error>;
    auto addMaterial(const std::shared_ptr<assets::Material>& material) -> void;
    auto removeMaterial(const assets::Material* material) -> void;
    /// @brief Appends 'instance' to a set, at the id's location.
    auto insert(InstanceId id, Instance instance, bool dynamic) -> void;
    /// @brief Takes the instance out of its set, moving the set's last one into its position.
    auto extract(InstanceId id) -> Instance;
    auto updateOffsets() -> void;

    Set m_static;
    Set m_dynamic;
    std::vector<std::optional<Location>> m_locations; // By InstanceId::index
    std::vector<std::uint32_t> m_freeIds;
    std::vector<DrawBatch> m_batches;
    std::unordered_map<const assets::MaterialTemplate*, std::uint32_t> m_batchIds;
    std::vector<std::shared_ptr<assets::Material>> m_materials;
    std::unordered_map<const assets::Material*, MaterialUse> m_materialUses;
};
}
