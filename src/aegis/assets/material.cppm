export module aegis.assets:material;
import std;
import :texture;
import aegis.assets.data;
import aegis.rhi;
import aegis.math;

export namespace aegis::assets
{
enum class MaterialType
{
    Opaque,
    Transparent,
};

using MaterialValue = std::variant<std::int32_t, std::uint32_t, float, glm::vec2, glm::vec3, glm::vec4,
    std::shared_ptr<Texture>>;

/// @brief Where a parameter lives in a material's parameter block, laid out with scalar rules
///        (C++ packing, as the new stack's shaders read blocks; see shaders/modules/README.md).
///        A texture takes 8 bytes: its SampledImageDescriptor index, then its SamplerDescriptor index.
struct MaterialParameter
{
    std::size_t offset;
    std::size_t size;
    MaterialValue defaultValue;
};

/// @brief The parameters a kind of material has, their defaults and their block layout. Pipelines are
///        not part of it: they belong to the render passes that draw the material (Phase 3).
class MaterialTemplate
{
public:
    explicit MaterialTemplate(MaterialType type = MaterialType::Opaque) :
        m_type{ type }
    {
    }

    /// @brief Appends a parameter after the existing ones.
    /// @return ErrorCode::InvalidArgument if 'name' exists or a texture default is null.
    [[nodiscard]] auto addParameter(const std::string& name, MaterialValue defaultValue) -> std::expected<void, Error>;

    [[nodiscard]] auto type() const noexcept -> MaterialType { return m_type; }
    [[nodiscard]] auto parameterSize() const noexcept -> std::size_t { return m_parameterSize; }
    [[nodiscard]] auto hasParameter(const std::string& name) const -> bool { return m_parameters.contains(name); }

    /// @return nullptr if the template has no parameter 'name'.
    [[nodiscard]] auto parameter(const std::string& name) const -> const MaterialParameter*;

    /// @brief Whether 'value' may override parameter 'name': the parameter exists, 'value' holds the
    ///        type of its default, and a texture is not null.
    /// @return ErrorCode::InvalidArgument otherwise.
    [[nodiscard]] auto check(const std::string& name, const MaterialValue& value) const -> std::expected<void, Error>;

    [[nodiscard]] auto parameters() const noexcept -> const std::unordered_map<std::string, MaterialParameter>&
    {
        return m_parameters;
    }

private:
    MaterialType m_type;
    std::unordered_map<std::string, MaterialParameter> m_parameters;
    std::size_t m_parameterSize{ 0 };
};

/// @brief What decides how a material draws: its template, alpha mode and cull mode. Materials with the same variant
///        draw in one draw batch (render_graph::InstanceStore) with the same pipelines. The template's
///        default is opaque and back-face culled.
struct MaterialVariant
{
    std::shared_ptr<const MaterialTemplate> materialTemplate;
    AlphaMode alphaMode{ AlphaMode::Opaque };
    rhi::CullMode cullMode{ rhi::CullMode::Back };

    auto operator==(const MaterialVariant&) const -> bool = default;

    /// @brief For unordered containers: hashes the template by address.
    struct Hash
    {
        [[nodiscard]] auto operator()(const MaterialVariant& variant) const noexcept -> std::size_t
        {
            auto hash = std::hash<const MaterialTemplate*>{}(variant.materialTemplate.get());
            hash ^= (static_cast<std::size_t>(variant.alphaMode) << 1) ^ (static_cast<std::size_t>(variant.cullMode) << 4);
            return hash;
        }
    };
};

/// @brief A template's parameters with per-material overrides, in a GPU block per frame in flight
///        that shaders read through parameterAddress().
/// @note Holds the Device and frees its buffer on destruction (deferred until the GPU is done).
class Material
{
public:
    /// @brief The asset type's name, for the editor (reflect::FieldInfo::assetType)
    static constexpr std::string_view assetType{ "Material" };

    /// @param framesInFlight One parameter block per frame, so updating one never races a frame the
    ///        GPU is still reading (renderer::Renderer::maxFramesInFlight).
    [[nodiscard]] static auto create(rhi::Device& device, std::shared_ptr<const MaterialTemplate> materialTemplate,
        std::uint32_t framesInFlight) -> std::expected<std::shared_ptr<Material>, Error>;

    Material(const Material&) = delete;
    Material(Material&&) = delete;
    ~Material();

    auto operator=(const Material&) -> Material& = delete;
    auto operator=(Material&&) -> Material& = delete;

    [[nodiscard]] auto materialTemplate() const noexcept -> const std::shared_ptr<const MaterialTemplate>&
    {
        return m_template;
    }

    /// @brief The material's variant: its template with its alpha and cull mode.
    [[nodiscard]] auto variant() const -> MaterialVariant { return { m_template, m_alphaMode, m_cullMode }; }
    [[nodiscard]] auto alphaMode() const noexcept -> AlphaMode { return m_alphaMode; }
    [[nodiscard]] auto cullMode() const noexcept -> rhi::CullMode { return m_cullMode; }
    /// @brief Changes the variant. The material's instances move to the new variant's draw batch at the next
    ///        sync (render_graph::InstanceStore::syncVariants).
    auto setAlphaMode(AlphaMode mode) noexcept -> void { m_alphaMode = mode; }
    auto setCullMode(rhi::CullMode mode) noexcept -> void { m_cullMode = mode; }

    /// @brief A new material with this one's template, variant and overrides, in its own parameter buffer: what an
    ///        object changes on its own instead of the shared one.
    [[nodiscard]] auto copy() const -> std::expected<std::shared_ptr<Material>, Error>;

    /// @brief Overrides the template's default.
    /// @return ErrorCode::InvalidArgument, leaving the material unchanged, if MaterialTemplate::check
    ///         rejects 'value'.
    [[nodiscard]] auto setParameter(const std::string& name, MaterialValue value) -> std::expected<void, Error>;

    /// @brief The override if set, otherwise the template's default.
    /// @return nullptr if the template has no parameter 'name'.
    [[nodiscard]] auto parameter(const std::string& name) const -> const MaterialValue*;

    /// @return nullptr if the template has no parameter 'name', or it does not hold a T.
    template<typename T>
    [[nodiscard]] auto parameter(const std::string& name) const -> const T*
    {
        const auto* value = parameter(name);
        return value ? std::get_if<T>(value) : nullptr;
    }

    /// @brief Rewrites the block of frame slot 'frameIndex' if parameters changed since that slot was
    ///        last written. Call before recording a frame that reads parameterAddress(frameIndex).
    auto update(std::uint32_t frameIndex) -> void;

    [[nodiscard]] auto parameterAddress(std::uint32_t frameIndex) const -> rhi::DeviceAddress;

private:
    Material(rhi::Device& device, std::shared_ptr<const MaterialTemplate> materialTemplate,
        rhi::BufferHandle buffer, std::size_t blockStride, std::uint32_t framesInFlight);

    rhi::Device& m_device;
    std::shared_ptr<const MaterialTemplate> m_template;
    std::unordered_map<std::string, MaterialValue> m_overrides;
    AlphaMode m_alphaMode{ AlphaMode::Opaque };
    rhi::CullMode m_cullMode{ rhi::CullMode::Back };
    rhi::BufferHandle m_buffer;
    std::size_t m_blockStride;
    std::vector<bool> m_dirty; // Per frame slot
};
}

export namespace aegis::assets::detail
{
[[nodiscard]] auto scalarAlignment(const MaterialValue& value) -> std::size_t;
[[nodiscard]] auto scalarSize(const MaterialValue& value) -> std::size_t;

/// @brief Serializes every parameter of 'materialTemplate' into 'block' (at least parameterSize()
///        bytes): the override from 'overrides' if present, otherwise the default. A null texture
///        is written as invalid indices (0xFFFFFFFF).
auto writeParameters(const MaterialTemplate& materialTemplate,
    const std::unordered_map<std::string, MaterialValue>& overrides, std::span<std::byte> block) -> void;
}
