export module aegis.render_passes:ui_pass;
import std;
import aegis.render_graph;
import aegis.rhi;

export namespace aegis::render_graph
{
/// @brief Draws ImGui over "Final" before it is presented, or, rendering into a view (Desc::targetFormat),
///        onto the frame's target while reading "Final", so the scene passes come first. Owns ImGui's Vulkan
///        backend on the device: initialized in createResources, shut down with the pass.
/// @note The ImGui context, any platform backend (e.g. ImGui_ImplGlfw) and the frame's UI belong to the
///       caller: it builds the UI and calls ImGui::Render before the graph executes, so the pass only
///       records the draw data. The context must outlive the pass.
class UiPass : public RenderPass
{
public:
    struct Desc
    {
        /// @brief ImGui's backend takes the instance; needed by createResources, so a graph can be declared without it.
        const rhi::Context* context{ nullptr };
        /// @brief Draw onto the frame's target, of this format and cleared first, and sample "Final" (a view's
        ///        target, which the UI shows) instead of drawing over it.
        std::optional<rhi::Format> targetFormat;
    };

    /// @brief Descriptor sets the backend may allocate: the font atlas plus textures the UI shows.
    static constexpr std::uint32_t descriptorPoolSize{ 64 };

    UiPass(ResourceDeclarations& declarations, Desc desc);
    ~UiPass() override;

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto createResources(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    const rhi::Context* m_context;
    std::optional<rhi::Format> m_targetFormat; // Drawing onto the frame's target
    ResourceId m_final;
    bool m_initialized{ false };
};

/// @brief Image views the UI shows (ImGui::Image), registered with the UI pass's ImGui backend. Ask for a view's
///        texture ID in every frame that draws it: a registration no frame asked for during the frames in flight
///        is released, and the IDs change whenever the registrations are released.
/// @note Registering needs a UI pass whose resources are created; release every registration before that pass goes
///       (it takes the backend's descriptor pool along) and when the views it shows are recreated (a resize).
class UiTextures
{
public:
    [[nodiscard]] static auto create(rhi::Device& device) -> std::expected<UiTextures, Error>;

    UiTextures(const UiTextures&) = delete;
    UiTextures(UiTextures&& other) noexcept;
    ~UiTextures();

    auto operator=(const UiTextures&) -> UiTextures& = delete;
    auto operator=(UiTextures&&) -> UiTextures& = delete;

    /// @brief The ImTextureID showing 'view', registered on first use, valid for this frame; 0 without a UI pass.
    /// @note The UI pass samples it in a fragment shader: the frame graph must have the view's image in a shader
    ///       read state by then, as a view's "Final" is (UiPass::Desc::targetFormat).
    [[nodiscard]] auto get(rhi::ImageViewHandle view) -> std::uint64_t;
    /// @brief Starts a frame: releases the registrations no frame still in flight samples.
    auto nextFrame() -> void;
    /// @brief Releases every registration now. The GPU must be done with them (idle).
    auto releaseAll() -> void;
    /// @brief The registrations held.
    [[nodiscard]] auto size() const noexcept -> std::size_t { return m_registrations.size(); }

private:
    struct Registration
    {
        rhi::ImageViewHandle view;
        std::uint64_t id;
        std::uint64_t lastFrame; // The last frame that asked for it
        std::uint64_t backend;   // The backend generation it was registered with
    };

    UiTextures(rhi::Device& device, rhi::SamplerHandle sampler);

    auto release(const Registration& registration) const -> void;

    rhi::Device* m_device;
    rhi::SamplerHandle m_sampler;              // Null once moved from
    std::vector<Registration> m_registrations; // Few, so searched in order
    std::uint64_t m_frame{ 0 };
};
}
