export module aegis.assets:material;
import std;
import :error;
import :texture;
import aegis.rhi;
import aegis.core.asset;
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
class MaterialTemplate : public aegis::core::Asset
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

/// @brief A template's parameters with per-material overrides, in a GPU block per frame in flight
///        that shaders read through parameterAddress().
/// @note Holds the Device and frees its buffer on destruction (deferred until the GPU is done).
class MaterialInstance : public aegis::core::Asset
{
public:
    /// @param framesInFlight One parameter block per frame, so updating one never races a frame the
    ///        GPU is still reading (renderer::Renderer::maxFramesInFlight).
    [[nodiscard]] static auto create(rhi::Device& device, std::shared_ptr<const MaterialTemplate> materialTemplate,
        std::uint32_t framesInFlight) -> std::expected<std::shared_ptr<MaterialInstance>, Error>;

    MaterialInstance(const MaterialInstance&) = delete;
    MaterialInstance(MaterialInstance&&) = delete;
    ~MaterialInstance() override;

    auto operator=(const MaterialInstance&) -> MaterialInstance& = delete;
    auto operator=(MaterialInstance&&) -> MaterialInstance& = delete;

    [[nodiscard]] auto materialTemplate() const noexcept -> const std::shared_ptr<const MaterialTemplate>&
    {
        return m_template;
    }

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
    MaterialInstance(rhi::Device& device, std::shared_ptr<const MaterialTemplate> materialTemplate,
        rhi::BufferHandle buffer, std::size_t blockStride, std::uint32_t framesInFlight);

    rhi::Device& m_device;
    std::shared_ptr<const MaterialTemplate> m_template;
    std::unordered_map<std::string, MaterialValue> m_overrides;
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
