export module aegis.rhi:imgui_backend;
import std;
import :common;
import :fwd;
import :resource_handle;
import aegis.rhi.types;

export namespace aegis::rhi
{
/// @brief ImGui's Vulkan backend (ImGui_ImplVulkan) on a device: the one place that hands raw Vulkan handles to it, so
///        nothing outside the rhi names a Vulkan type for the UI. Created once the device exists and kept for as long
///        as the UI draws; a frame graph that comes and goes only records through it.
/// @note The ImGui context belongs to the caller and must exist for the backend's lifetime. At most one backend can
///       exist per ImGui context. Destroy it while the device exists, and before the context.
class ImGuiBackend
{
public:
    struct Desc
    {
        /// @brief Sets ImGui may allocate: the font atlas plus every texture the UI shows (addTexture).
        std::uint32_t descriptorPoolSize{ 64 };
        /// @brief ImGui cycles one set of vertex and index buffers per frame in flight.
        std::uint32_t framesInFlight{ 2 };
    };

    /// @brief Initializes the backend. It draws nothing before setTargetFormat.
    [[nodiscard]] static auto create(const Context& context, Device& device, const Desc& desc) -> std::expected<std::unique_ptr<ImGuiBackend>, Error>;

    ImGuiBackend(const ImGuiBackend&) = delete;
    ImGuiBackend(ImGuiBackend&&) = delete;
    ~ImGuiBackend();

    auto operator=(const ImGuiBackend&) -> ImGuiBackend& = delete;
    auto operator=(ImGuiBackend&&) -> ImGuiBackend& = delete;

    /// @brief Sets the format of the color attachment record draws onto; the backend's pipeline is rebuilt when it
    ///        changes and kept otherwise. The GPU must not be using the old pipeline.
    [[nodiscard]] auto setTargetFormat(Format format) -> std::expected<void, Error>;
    /// @return Whether the caller ended an ImGui frame (ImGui::Render) whose draw data record can draw.
    [[nodiscard]] auto hasDrawData() const -> bool;
    /// @brief Draws the draw data of the frame the caller ended into the command buffer's current rendering, which
    ///        has one color attachment of the target format. An empty frame still goes through the backend, which
    ///        uploads the font atlas and other textures with it.
    auto record(const CommandBuffer& cmd) -> void;

    /// @brief Registers 'view', sampled through 'sampler' in the layout every shader read state has, for ImGui::Image.
    /// @return The ImTextureID, or 0 when the backend is out of descriptor sets.
    [[nodiscard]] auto addTexture(Device& device, SamplerHandle sampler, ImageViewHandle view) -> std::uint64_t;
    /// @brief Releases a texture addTexture returned. The GPU must be done with it.
    auto removeTexture(std::uint64_t texture) -> void;

private:
    ImGuiBackend() = default;

    std::optional<Format> m_targetFormat;
};
}
