export module aegis.render_passes:pipeline_library;
import std;
import :ibl;
import :shader;
import aegis.assets;
import aegis.render_graph;
import aegis.rhi;

export namespace aegis::render_graph
{
/// @brief The passes' pipelines and the BRDF lookup table, shared by every pass instance of every frame graph: one
///        pipeline per key, created the first time a pass asks for it and kept until the library goes. The engine owns
///        one that outlives its frame graphs (DefaultPassesDesc::pipelines), so a view's passes reuse another view's
///        pipelines and a rebuilt graph creates none. Passes keep a reference.
///
///        A compute pipeline's key is its shader path and entry point; a graphics pipeline's, its stages and the
///        rhi::Pipeline::GraphicsDesc fields that decide it (detail::graphicsPipelineKey): not its name, so the first
///        pass to ask names it. The library binds to the device of its first lookup; every later one passes the same.
class PipelineLibrary
{
public:
    PipelineLibrary();
    PipelineLibrary(const PipelineLibrary&) = delete;
    PipelineLibrary(PipelineLibrary&&) = delete;
    ~PipelineLibrary();

    auto operator=(const PipelineLibrary&) -> PipelineLibrary& = delete;
    auto operator=(PipelineLibrary&&) -> PipelineLibrary& = delete;

    /// @brief The compute pipeline of 'shaderPath' (see loadShader) and 'entryPoint', created as 'name' if missing.
    /// @return Valid while the library lives.
    [[nodiscard]] auto compute(rhi::Device& device, std::string_view name, std::string_view shaderPath,
        std::string_view entryPoint = "main") -> std::expected<const rhi::Pipeline*, Error>;

    /// @brief The graphics pipeline of 'stages' and 'desc' (whose shaders are filled in here), created if missing.
    /// @return Valid while the library lives.
    [[nodiscard]] auto graphics(rhi::Device& device, const rhi::Pipeline::GraphicsDesc& desc,
        std::span<const ShaderEntry> stages) -> std::expected<const rhi::Pipeline*, Error>;

    /// @brief The usual case: a vertex and a fragment stage of 'shaderPath', "vertexMain" and "fragmentMain".
    [[nodiscard]] auto graphics(rhi::Device& device, std::string_view shaderPath, const rhi::Pipeline::GraphicsDesc& desc)
        -> std::expected<const rhi::Pipeline*, Error>;

    /// @brief The BRDF lookup table (Ibl::brdfLut), computed on the first call, which also creates the Ibl that
    ///        computes it; later calls return the same texture and submit nothing.
    [[nodiscard]] auto brdfLut(rhi::Device& device) -> std::expected<std::shared_ptr<assets::Texture>, Error>;
    /// @brief The BRDF lookup table if brdfLut computed it, else null. Computes nothing.
    [[nodiscard]] auto computedBrdfLut() const noexcept -> const std::shared_ptr<assets::Texture>& { return m_brdfLut; }

    /// @brief The compute and graphics pipelines it holds (not its Ibl's). Pipelines are never removed, so an unchanged
    ///        size means no lookup created one.
    [[nodiscard]] auto size() const noexcept -> std::size_t { return m_pipelines.size(); }

private:
    auto bind(rhi::Device& device) -> void;

    rhi::Device* m_device{ nullptr };
    std::unordered_map<std::string, rhi::Pipeline> m_pipelines; // By key; nodes keep their address
    std::optional<Ibl> m_ibl;
    std::shared_ptr<assets::Texture> m_brdfLut;
};
}

export namespace aegis::render_graph::detail
{
/// @brief The key of a compute pipeline in a PipelineLibrary: its shader path and entry point.
[[nodiscard]] auto computePipelineKey(std::string_view shaderPath, std::string_view entryPoint) -> std::string;

/// @brief The key of a graphics pipeline in a PipelineLibrary: each stage (stage, shader path, entry point), and the
///        fields of 'desc' that decide the pipeline: the color attachments' formats and blends, the depth and stencil
///        formats, the depth state, cull mode, front face and vertex input. Not its name, nor desc.shaders (the
///        stages' code).
[[nodiscard]] auto graphicsPipelineKey(const rhi::Pipeline::GraphicsDesc& desc, std::span<const ShaderEntry> stages)
    -> std::string;
}
