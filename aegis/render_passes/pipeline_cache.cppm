module;
#include <cassert>
export module aegis.render_passes:pipeline_cache;
import std;
import :instance_store;
import aegis.assets;
import aegis.render_graph;
import aegis.rhi;

export namespace aegis::render_graph
{
/// @brief How a pass draws a material: into the G-buffer, its depth only (a shadow view), or lit and blended
///        into the scene color (the forward technique, which draws the blended materials the G-buffer can't hold).
enum class Technique
{
    GBuffer,
    Depth,
    Forward,
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

/// @brief The fixed-function state of a pipeline that its key decides: rhi::Pipeline::GraphicsDesc's cull mode, front
///        face and depth writes, and the blend of every color attachment.
struct PipelineState
{
    rhi::CullMode cullMode{ rhi::CullMode::Back };
    rhi::FrontFace frontFace{ rhi::FrontFace::CounterClockwise };
    rhi::BlendMode blend{ rhi::BlendMode::None };
    bool depthWrite{ true };

    auto operator==(const PipelineState&) const -> bool = default;
};

/// @brief The state a pipeline of 'key' is created with. The G-buffer holds surfaces, never a blend of them, and the
///        depth technique has no color to blend, so neither blends whatever the variant's alpha mode. The forward
///        technique blends with straight alpha and tests depth without writing it, so what it draws hides nothing
///        drawn after it.
[[nodiscard]] constexpr auto pipelineState(const PipelineKey& key) noexcept -> PipelineState
{
    PipelineState state{ .cullMode = key.variant.cullMode, .frontFace = key.frontFace };
    switch (key.technique)
    {
    case Technique::GBuffer:
    case Technique::Depth:
        break;
    case Technique::Forward:
        state.blend = rhi::BlendMode::Alpha;
        state.depthWrite = false;
        break;
    }
    return state;
}

/// @brief The fragment entry point a pipeline of 'technique' for 'variant' draws with, in its technique file: the
///        G-buffer's and the forward technique's "fragmentMain"; for depth, "depthFragmentMain" in the masked build,
///        which alpha-tests, and none (a pipeline without a fragment stage) otherwise.
[[nodiscard]] constexpr auto fragmentEntry(Technique technique, const assets::MaterialVariant& variant) noexcept
    -> std::optional<std::string_view>
{
    switch (technique)
    {
    case Technique::GBuffer:
    case Technique::Forward:
        return "fragmentMain";
    case Technique::Depth:
        if (variant.alphaMode == assets::AlphaMode::Mask)
            return "depthFragmentMain";
        return std::nullopt;
    }
    return std::nullopt;
}

/// @brief The SPIR-V file of 'technique' (its path in the shader folder, without .slang) built for 'variant'
///        (aegis_add_shaders TECHNIQUES): with its template's surface shader, and masked if its alpha mode is
///        AlphaMode::Mask. "gpu_driven/geometry" and an unlit variant give "gpu_driven/geometry.unlit.spv", or
///        "gpu_driven/geometry.unlit.masked.spv" masked.
[[nodiscard]] inline auto techniqueShader(std::string_view technique, const assets::MaterialVariant& variant) -> std::string
{
    assert(variant.materialTemplate && "a material variant names its template");
    const std::string_view masked = variant.alphaMode == assets::AlphaMode::Mask ? ".masked" : "";
    return std::format("{}.{}{}.spv", technique, variant.materialTemplate->surface(), masked);
}

/// @brief Whether the G-buffer technique draws 'batch': it has instances, its template has MaterialType 'type',
///        and it does not blend (blending needs the forward technique). The depth technique draws the same batches:
///        what the G-buffer holds casts shadows.
[[nodiscard]] inline auto drawsInGBuffer(const DrawBatch& batch, assets::MaterialType type = assets::MaterialType::Opaque)
    -> bool
{
    return batch.instanceCount > 0 && batch.variant.materialTemplate->type() == type &&
           batch.variant.alphaMode != assets::AlphaMode::Blend;
}

/// @brief Whether the forward technique draws 'batch': it has instances and blends, whatever its template's
///        MaterialType. Blended batches cast no shadows: the depth technique draws the G-buffer's batches only.
[[nodiscard]] inline auto drawsForward(const DrawBatch& batch) -> bool
{
    return batch.instanceCount > 0 && batch.variant.alphaMode == assets::AlphaMode::Blend;
}

/// @brief Whether a pass of 'technique' draws 'batch': drawsForward for the forward technique, drawsInGBuffer (with
///        'type') for the others.
[[nodiscard]] inline auto drawsBatch(Technique technique, const DrawBatch& batch,
    assets::MaterialType type = assets::MaterialType::Opaque) -> bool
{
    return technique == Technique::Forward ? drawsForward(batch) : drawsInGBuffer(batch, type);
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
