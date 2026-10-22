module;
#include <cassert>

module aegis.assets;
import std;
import aegis.core;
import aegis.rhi;
import aegis.math;

namespace aegis::assets
{
namespace
{
auto typeName(const MaterialValue& value) -> std::string_view
{
    // In MaterialValue's alternative order.
    constexpr std::array names{ "int32", "uint32", "float", "vec2", "vec3", "vec4", "texture" };
    static_assert(names.size() == std::variant_size_v<MaterialValue>);
    return names[value.index()];
}

auto isNullTexture(const MaterialValue& value) -> bool
{
    const auto* texture = std::get_if<std::shared_ptr<Texture>>(&value);
    return texture && !*texture;
}
}

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
                return sizeof(rhi::TextureDescriptor);
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
                    const auto descriptor = v ? v->descriptor() : rhi::TextureDescriptor{ { invalid }, { invalid } };
                    std::memcpy(dst, &descriptor, sizeof(descriptor));
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

auto MaterialTemplate::addParameter(const std::string& name, MaterialValue defaultValue) -> std::expected<void, Error>
{
    if (m_parameters.contains(name))
        return makeError(ErrorCode::InvalidArgument, std::format("Material parameter '{}' already exists", name));
    if (isNullTexture(defaultValue))
        return makeError(ErrorCode::InvalidArgument, std::format("Material parameter '{}' needs a default texture", name));

    const auto offset = core::alignTo(m_parameterSize, detail::scalarAlignment(defaultValue));
    const auto size = detail::scalarSize(defaultValue);
    m_parameterSize = offset + size;
    m_parameters.emplace(name, MaterialParameter{ .offset = offset, .size = size, .defaultValue = std::move(defaultValue) });
    return {};
}

auto MaterialTemplate::parameter(const std::string& name) const -> const MaterialParameter*
{
    const auto found = m_parameters.find(name);
    return found != m_parameters.end() ? &found->second : nullptr;
}

auto MaterialTemplate::check(const std::string& name, const MaterialValue& value) const -> std::expected<void, Error>
{
    const auto* parameter = this->parameter(name);
    if (!parameter)
        return makeError(ErrorCode::InvalidArgument, std::format("Material has no parameter '{}'", name));
    if (parameter->defaultValue.index() != value.index())
    {
        return makeError(ErrorCode::InvalidArgument, std::format("Material parameter '{}' is a {}, not a {}", name,
                                                         typeName(parameter->defaultValue), typeName(value)));
    }
    if (isNullTexture(value))
        return makeError(ErrorCode::InvalidArgument, std::format("Material parameter '{}' needs a texture, not null", name));
    return {};
}

auto Material::create(rhi::Device& device, std::shared_ptr<const MaterialTemplate> materialTemplate,
    std::uint32_t framesInFlight) -> std::expected<std::shared_ptr<Material>, Error>
{
    assert(materialTemplate && framesInFlight > 0);

    // 16-byte blocks: every frame's block starts at least as aligned as any member needs.
    const auto blockStride = core::alignTo(std::max<std::size_t>(materialTemplate->parameterSize(), 16), 16);
    auto buffer = device.createBuffer({
        .name = "MaterialParameters",
        .size = blockStride * framesInFlight,
        .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::ShaderDeviceAddress,
        .memory = rhi::MemoryUsage::CpuWrite,
    });
    if (!buffer)
        return makeError(buffer.error(), "Failed to create material parameter buffer");

    return std::shared_ptr<Material>{
        new Material{ device, std::move(materialTemplate), *buffer, blockStride, framesInFlight }
    };
}

Material::Material(rhi::Device& device, std::shared_ptr<const MaterialTemplate> materialTemplate,
    rhi::BufferHandle buffer, std::size_t blockStride, std::uint32_t framesInFlight) :
    m_device{ device },
    m_template{ std::move(materialTemplate) },
    m_buffer{ buffer },
    m_blockStride{ blockStride },
    m_dirty(framesInFlight, true)
{
}

Material::~Material()
{
    m_device.free(m_buffer);
}

auto Material::copy() const -> std::expected<std::shared_ptr<Material>, Error>
{
    auto material = create(m_device, m_template, static_cast<std::uint32_t>(m_dirty.size()));
    if (material)
        (*material)->m_overrides = m_overrides;
    return material;
}

auto Material::setParameter(const std::string& name, MaterialValue value) -> std::expected<void, Error>
{
    if (auto valid = m_template->check(name, value); !valid)
        return valid;

    m_overrides.insert_or_assign(name, std::move(value));
    std::ranges::fill(m_dirty, true);
    return {};
}

auto Material::parameter(const std::string& name) const -> const MaterialValue*
{
    if (const auto found = m_overrides.find(name); found != m_overrides.end())
        return &found->second;

    const auto* parameter = m_template->parameter(name);
    return parameter ? &parameter->defaultValue : nullptr;
}

auto Material::update(std::uint32_t frameIndex) -> void
{
    if (!m_dirty[frameIndex])
        return;

    std::vector<std::byte> block(m_blockStride);
    detail::writeParameters(*m_template, m_overrides, block);
    m_device.get(m_buffer).write(block.data(), block.size(), frameIndex * m_blockStride);
    m_dirty[frameIndex] = false;
}

auto Material::parameterAddress(std::uint32_t frameIndex) const -> rhi::DeviceAddress
{
    return m_device.get(m_buffer).deviceAddress() + frameIndex * m_blockStride;
}
}
