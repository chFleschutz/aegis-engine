export module aegis.rhi:sampler;
import std;
import :bindless_heap;
import :common;
import :error;
import :fwd;
import vulkan;

export namespace aegis::rhi::detail
{
/// @brief Resolves a requested maximum anisotropy against the device limit.
/// @return The anisotropy to sample with, or nullopt if anisotropic filtering should be disabled
///         because 'requested' is 1 or less, or the device does not support it.
/// @note Takes plain values rather than vk::PhysicalDeviceLimits: a test TU cannot name a vk:: type.
[[nodiscard]] auto resolveAnisotropy(float requested, float deviceMax) -> std::optional<float>;
}

export namespace aegis::rhi
{
class Sampler
{
    friend Device;

public:
    /// @brief Disables the upper LOD clamp, equal to VK_LOD_CLAMP_NONE. The default, so that one
    ///        sampler serves images of any mip count; the image view bounds the mips instead.
    static constexpr float LodClampNone{ 1000.0f };

    struct Desc
    {
        std::string_view name;
        Filter magFilter{ Filter::Linear };
        Filter minFilter{ Filter::Linear };
        SamplerMipmapMode mipmapMode{ SamplerMipmapMode::Linear };
        SamplerAddressMode addressMode{ SamplerAddressMode::Repeat };
        float maxAnisotropy{ 16.0f }; // 1 or less disables, larger is clamped to the device limit
        float minLod{ 0.0f };
        float maxLod{ LodClampNone };
    };

    [[nodiscard]] auto vk() const noexcept -> vk::Sampler { return *m_sampler; }
    [[nodiscard]] auto descriptor() const noexcept -> SamplerDescriptor { return m_descriptor; }

private:
    [[nodiscard]] static auto create(const Device& device, const Desc& desc)
        -> std::expected<Sampler, Error>;

    explicit Sampler(vk::raii::Sampler sampler);

    vk::raii::Sampler m_sampler;
    SamplerDescriptor m_descriptor{};
};
}
