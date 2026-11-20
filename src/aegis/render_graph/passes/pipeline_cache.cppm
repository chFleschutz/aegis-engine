export module aegis.render_passes:pipeline_cache;
import std;
import :instance_store;
import aegis.assets;
import aegis.render_graph;
import aegis.rhi;

export namespace aegis::render_graph
{
/// @brief How a pass draws a material: into the G-buffer today; depth-only and forward later.
enum class Technique
{
    GBuffer,
};

/// @brief Which shaders a geometry pass draws with: vertex shaders, one draw per instance (CPU-driven), or task and
///        mesh shaders, one indirect draw per draw batch (GPU-driven).
enum class GeometryPath
{
    CpuDriven,
    GpuDriven,
};

/// @brief Everything that picks one pipeline for a draw batch. The CPU-driven path draws mirrored instances
///        (InstanceFlags::Mirrored) with a clockwise front face; the GPU-driven path flips their winding in the
///        mesh shader instead, so its keys are always counter-clockwise.
struct PipelineKey
{
    Technique technique{ Technique::GBuffer };
    assets::MaterialVariant variant;
    GeometryPath path{ GeometryPath::CpuDriven };
    rhi::FrontFace frontFace{ rhi::FrontFace::CounterClockwise };

    auto operator==(const PipelineKey&) const -> bool = default;

    struct Hash
    {
        [[nodiscard]] auto operator()(const PipelineKey& key) const noexcept -> std::size_t
        {
            auto hash = assets::MaterialVariant::Hash{}(key.variant);
            hash ^= (static_cast<std::size_t>(key.technique) << 8) ^ (static_cast<std::size_t>(key.path) << 12) ^
                    (static_cast<std::size_t>(key.frontFace) << 13);
            return hash;
        }
    };
};

/// @brief The fixed-function state of a pipeline that its key decides: rhi::Pipeline::GraphicsDesc's cull mode and
///        front face, and the blend of every color attachment.
struct PipelineState
{
    rhi::CullMode cullMode{ rhi::CullMode::Back };
    rhi::FrontFace frontFace{ rhi::FrontFace::CounterClockwise };
    rhi::BlendMode blend{ rhi::BlendMode::None };

    auto operator==(const PipelineState&) const -> bool = default;
};

/// @brief The state a pipeline of 'key' is created with. The G-buffer holds surfaces, never a blend of them, so its
///        technique blends nothing whatever the variant's alpha mode.
[[nodiscard]] constexpr auto pipelineState(const PipelineKey& key) noexcept -> PipelineState
{
    rhi::BlendMode blend{ rhi::BlendMode::None };
    switch (key.technique)
    {
    case Technique::GBuffer:
        blend = rhi::BlendMode::None;
        break;
    }
    return { .cullMode = key.variant.cullMode, .frontFace = key.frontFace, .blend = blend };
}

/// @brief The SPIR-V file of 'technique' (its path in the shader folder, without .slang) built for 'variant'
///        (aegis_add_shaders TECHNIQUES): with its template's surface shader, and masked if its alpha mode is
///        AlphaMode::Mask. "gpu-driven/geometry" and an unlit variant give "gpu-driven/geometry.unlit.spv", or
///        "gpu-driven/geometry.unlit.masked.spv" masked.
[[nodiscard]] inline auto techniqueShader(std::string_view technique, const assets::MaterialVariant& variant) -> std::string
{
    const std::string_view masked = variant.alphaMode == assets::AlphaMode::Mask ? ".masked" : "";
    return std::format("{}.{}{}.spv", technique, variant.materialTemplate->surface(), masked);
}

/// @brief Whether the G-buffer technique draws 'batch': it has instances, its template has MaterialType 'type',
///        and it does not blend (blending needs the forward technique).
[[nodiscard]] inline auto drawsInGBuffer(const DrawBatch& batch, assets::MaterialType type = assets::MaterialType::Opaque)
    -> bool
{
    return batch.instanceCount > 0 && batch.variant.materialTemplate->type() == type &&
           batch.variant.alphaMode != assets::AlphaMode::Blend;
}

/// @brief A pass's pipelines by PipelineKey. Each is created the first time its key is prepared, before recording
///        (RenderPass::prepare), and kept until the cache goes with its pass, when the graph goes. A template over
///        what it holds so tests can cache plain values.
template<typename T = rhi::Pipeline>
class PipelineCache
{
public:
    using Create = std::function<std::expected<T, Error>(const PipelineKey&)>;

    explicit PipelineCache(Create create) :
        m_create{ std::move(create) }
    {
    }

    /// @brief Creates the pipeline of 'key' unless the cache holds it.
    /// @return The creation's error; the key stays missing then, and the next prepare tries again.
    [[nodiscard]] auto prepare(const PipelineKey& key) -> std::expected<void, Error>
    {
        if (m_pipelines.contains(key))
            return {};
        auto created = m_create(key);
        if (!created)
            return std::unexpected{ created.error() };
        m_pipelines.emplace(key, std::move(*created));
        return {};
    }

    /// @return nullptr if 'key' was not prepared. The pointer stays valid while the cache lives.
    [[nodiscard]] auto find(const PipelineKey& key) const -> const T*
    {
        const auto found = m_pipelines.find(key);
        return found != m_pipelines.end() ? &found->second : nullptr;
    }

    [[nodiscard]] auto contains(const PipelineKey& key) const -> bool { return m_pipelines.contains(key); }
    [[nodiscard]] auto size() const noexcept -> std::size_t { return m_pipelines.size(); }

private:
    Create m_create;
    std::unordered_map<PipelineKey, T, PipelineKey::Hash> m_pipelines;
};
}
