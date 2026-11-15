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
    return parameterTypeName(parameterType(value));
}

// The parameter's value for 'defaultValue' from a template file: a DefaultTexture becomes its texture.
auto resolve(const ParameterDefault& defaultValue, const DefaultTextures& textures) -> MaterialValue
{
    return std::visit(
        [&]<typename T>(const T& value) -> MaterialValue {
            if constexpr (std::is_same_v<T, DefaultTexture>)
            {
                switch (value)
                {
                case DefaultTexture::White: return textures.white;
                case DefaultTexture::Black: return textures.black;
                case DefaultTexture::Normal: return textures.normal;
                }
                return std::shared_ptr<Texture>{};
            }
            else
            {
                return value;
            }
        },
        defaultValue);
}

auto isNullTexture(const MaterialValue& value) -> bool
{
    const auto* texture = std::get_if<std::shared_ptr<Texture>>(&value);
    return texture && !*texture;
}
}

auto parameterType(const MaterialValue& value) -> ParameterType
{
    static_assert(std::variant_size_v<MaterialValue> == static_cast<std::size_t>(ParameterType::Texture) + 1);
    return static_cast<ParameterType>(value.index());
}

auto parameterTypeName(ParameterType type) -> std::string_view
{
    switch (type)
    {
    case ParameterType::Int: return "int";
    case ParameterType::UInt: return "uint";
    case ParameterType::Float: return "float";
    case ParameterType::Vec2: return "vec2";
    case ParameterType::Vec3: return "vec3";
    case ParameterType::Vec4: return "vec4";
    case ParameterType::Texture: return "texture";
    }
    return "unknown";
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

auto MaterialTemplate::create(const MaterialTemplateDesc& desc, const SurfaceBlock& block, const DefaultTextures& textures)
    -> std::expected<MaterialTemplate, Error>
{
    MaterialTemplate materialTemplate{ desc.surface };
    for (const auto& [name, defaultValue] : desc.parameters)
    {
        if (auto added = materialTemplate.addParameter(name, resolve(defaultValue, textures)); !added)
            return makeError(ErrorCode::InvalidData, added.error().message);
    }
    if (auto checked = materialTemplate.checkBlock(block); !checked)
        return std::unexpected{ checked.error() };
    return materialTemplate;
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

auto MaterialTemplate::checkBlock(const SurfaceBlock& block) const -> std::expected<void, Error>
{
    std::vector<std::pair<const std::string*, const MaterialParameter*>> ordered;
    ordered.reserve(m_parameters.size());
    for (const auto& [name, parameter] : m_parameters)
        ordered.emplace_back(&name, &parameter);
    std::ranges::sort(ordered, {}, [](const auto& entry) { return entry.second->offset; });

    for (std::size_t i = 0; i < ordered.size(); ++i)
    {
        const auto& name = *ordered[i].first;
        const auto& parameter = *ordered[i].second;
        const auto type = parameterType(parameter.defaultValue);
        if (i >= block.fields.size())
            return makeError(ErrorCode::InvalidData, std::format("parameter '{}' is not in the parameter block of surface shader '{}'", name, m_surface));

        const auto& field = block.fields[i];
        if (field.name != name)
        {
            return makeError(ErrorCode::InvalidData, std::format("parameter '{}' (offset {}) is where surface shader '{}' has '{}'",
                                                         name, parameter.offset, m_surface, field.name));
        }
        if (field.offset != parameter.offset)
        {
            return makeError(ErrorCode::InvalidData, std::format("parameter '{}' is at offset {}, surface shader '{}' reads it at {}",
                                                         name, parameter.offset, m_surface, field.offset));
        }
        if (field.type != type)
        {
            const auto shaderType = field.type ? std::format("a {}", parameterTypeName(*field.type)) : std::string{ "a type no parameter has" };
            return makeError(ErrorCode::InvalidData, std::format("parameter '{}' is a {}, surface shader '{}' reads {}",
                                                         name, parameterTypeName(type), m_surface, shaderType));
        }
        if (field.size != parameter.size)
        {
            return makeError(ErrorCode::InvalidData, std::format("parameter '{}' takes {} bytes, surface shader '{}' reads {}",
                                                         name, parameter.size, m_surface, field.size));
        }
    }
    if (block.fields.size() > ordered.size())
    {
        const auto& field = block.fields[ordered.size()];
        return makeError(ErrorCode::InvalidData, std::format("surface shader '{}' reads '{}' (offset {}), which the template has no parameter for",
                                                     m_surface, field.name, field.offset));
    }
    if (block.size != m_parameterSize)
    {
        return makeError(ErrorCode::InvalidData, std::format("the parameters take {} bytes, the parameter block of surface shader '{}' {}",
                                                     m_parameterSize, m_surface, block.size));
    }
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
    {
        (*material)->m_overrides = m_overrides;
        (*material)->m_alphaMode = m_alphaMode;
        (*material)->m_cullMode = m_cullMode;
    }
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
