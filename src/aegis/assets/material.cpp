module;
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

module aegis.assets;
import aegis.rhi;
import Aegis.Math;

namespace aegis::assets
{
namespace detail
{
auto scalarAlignment(const MaterialValue& /*value*/) -> std::size_t
{
    // Scalar layout aligns a vector like its components: every parameter type here has 4-byte ones.
    return 4;
}

auto scalarSize(const MaterialValue& value) -> std::size_t
{
    return std::visit(
        []<typename T>(const T&) -> std::size_t {
            if constexpr (std::is_same_v<T, std::shared_ptr<Texture>>)
                return 2 * sizeof(std::uint32_t);
            else
                return sizeof(T);
        },
        value);
}

auto writeParameters(const MaterialTemplate& materialTemplate,
    const std::unordered_map<std::string, MaterialValue>& overrides, std::span<std::byte> block) -> void
{
    assert(block.size() >= materialTemplate.parameterSize() && "Parameter block too small");

    for (const auto& [name, parameter] : materialTemplate.parameters())
    {
        const auto found = overrides.find(name);
        const auto& value = found != overrides.end() ? found->second : parameter.defaultValue;
        auto* dst = block.data() + parameter.offset;

        std::visit(
            [&]<typename T>(const T& v) {
                if constexpr (std::is_same_v<T, std::shared_ptr<Texture>>)
                {
                    constexpr auto invalid = ~std::uint32_t{ 0 };
                    const std::array indices{
                        v ? v->sampledDescriptor().index : invalid,
                        v ? v->samplerDescriptor().index : invalid,
                    };
                    std::memcpy(dst, indices.data(), sizeof(indices));
                }
                else
                {
                    std::memcpy(dst, &v, sizeof(T));
                }
            },
            value);
    }
}
}

auto MaterialTemplate::addParameter(const std::string& name, MaterialValue defaultValue) -> void
{
    assert(!m_parameters.contains(name) && "Material parameter already exists");
    assert((!std::holds_alternative<std::shared_ptr<Texture>>(defaultValue) ||
               std::get<std::shared_ptr<Texture>>(defaultValue)) &&
           "A texture parameter needs a default texture");

    const auto offset = rhi::utility::alignTo(m_parameterSize, detail::scalarAlignment(defaultValue));
    const auto size = detail::scalarSize(defaultValue);
    m_parameterSize = offset + size;
    m_parameters.emplace(name, MaterialParameter{ .offset = offset, .size = size, .defaultValue = std::move(defaultValue) });
}

auto MaterialTemplate::parameter(const std::string& name) const -> const MaterialParameter*
{
    const auto found = m_parameters.find(name);
    return found != m_parameters.end() ? &found->second : nullptr;
}

auto MaterialInstance::create(rhi::Device& device, std::shared_ptr<const MaterialTemplate> materialTemplate,
    std::uint32_t framesInFlight) -> std::expected<std::shared_ptr<MaterialInstance>, Error>
{
    using namespace rhi::utility;
    assert(materialTemplate && framesInFlight > 0);

    // 16-byte blocks: every frame's block starts at least as aligned as any member needs.
    const auto blockStride = alignTo(std::max<std::size_t>(materialTemplate->parameterSize(), 16), 16);
    auto buffer = device.createBuffer({
        .name = "MaterialParameters",
        .size = blockStride * framesInFlight,
        .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::ShaderDeviceAddress,
        .memory = rhi::MemoryUsage::CpuWrite,
    });
    if (!buffer)
        return makeError(buffer.error(), "Failed to create material parameter buffer");

    return std::shared_ptr<MaterialInstance>{
        new MaterialInstance{ device, std::move(materialTemplate), *buffer, blockStride, framesInFlight }
    };
}

MaterialInstance::MaterialInstance(rhi::Device& device, std::shared_ptr<const MaterialTemplate> materialTemplate,
    rhi::BufferHandle buffer, std::size_t blockStride, std::uint32_t framesInFlight) :
    m_device{ device },
    m_template{ std::move(materialTemplate) },
    m_buffer{ buffer },
    m_blockStride{ blockStride },
    m_dirty(framesInFlight, true)
{
}

MaterialInstance::~MaterialInstance()
{
    m_device.free(m_buffer);
}

auto MaterialInstance::setParameter(const std::string& name, MaterialValue value) -> void
{
    [[maybe_unused]] const auto* parameter = m_template->parameter(name);
    assert(parameter && "Material parameter not found");
    assert(parameter->defaultValue.index() == value.index() && "Material parameter type mismatch");

    m_overrides.insert_or_assign(name, std::move(value));
    std::ranges::fill(m_dirty, true);
}

auto MaterialInstance::parameter(const std::string& name) const -> const MaterialValue&
{
    if (const auto found = m_overrides.find(name); found != m_overrides.end())
        return found->second;

    const auto* parameter = m_template->parameter(name);
    assert(parameter && "Material parameter not found");
    return parameter->defaultValue;
}

auto MaterialInstance::update(std::uint32_t frameIndex) -> void
{
    if (!m_dirty[frameIndex])
        return;

    std::vector<std::byte> block(m_blockStride);
    detail::writeParameters(*m_template, m_overrides, block);
    m_device.get(m_buffer).write(block.data(), block.size(), frameIndex * m_blockStride);
    m_dirty[frameIndex] = false;
}

auto MaterialInstance::parameterAddress(std::uint32_t frameIndex) const -> rhi::DeviceAddress
{
    return m_device.get(m_buffer).deviceAddress() + frameIndex * m_blockStride;
}
}
