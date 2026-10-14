module;
#include <algorithm>
#include <expected>
#include <optional>

module aegis.rhi;
import :sampler;
import :debug;
import :vulkan_conversions;
import vulkan_hpp;

namespace aegis::rhi::detail
{
auto resolveAnisotropy(float requested, float deviceMax) -> std::optional<float>
{
    if (requested <= 1.0f || deviceMax <= 1.0f)
        return std::nullopt;

    return std::min(requested, deviceMax);
}
}

namespace aegis::rhi
{
static_assert(Sampler::LodClampNone == vk::LodClampNone);

auto Sampler::create(const Device& device, const Desc& desc) -> std::expected<Sampler, Error>
{
    const auto anisotropy = detail::resolveAnisotropy(desc.maxAnisotropy,
        device.properties().core.properties.limits.maxSamplerAnisotropy);
    const auto addressMode = toVulkan(desc.addressMode);

    vk::SamplerCreateInfo samplerInfo{
        .magFilter = toVulkan(desc.magFilter),
        .minFilter = toVulkan(desc.minFilter),
        .mipmapMode = toVulkan(desc.mipmapMode),
        .addressModeU = addressMode,
        .addressModeV = addressMode,
        .addressModeW = addressMode,
        .anisotropyEnable = anisotropy.has_value(),
        .maxAnisotropy = anisotropy.value_or(1.0f),
        .minLod = desc.minLod,
        .maxLod = desc.maxLod,
        .borderColor = vk::BorderColor::eFloatOpaqueBlack,
    };

    auto sampler = device->createSampler(samplerInfo);
    if (!sampler.has_value())
        return makeError(toRHI(sampler.result));

    debug::setName(*device, **sampler, desc.name);

    return Sampler{ std::move(*sampler) };
}

Sampler::Sampler(vk::raii::Sampler sampler) :
    m_sampler{ std::move(sampler) }
{
}
}
