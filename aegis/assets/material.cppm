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

using MaterialValue = std::variant<std::int32_t, std::uint32_t, float, math::Float2, math::Float3, math::Float4,
    std::shared_ptr<Texture>>;

/// @brief A material parameter's type, in MaterialValue's alternative order.
enum class ParameterType
{
    Int,
    UInt,
    Float,
    Vec2,
    Vec3,
    Vec4,
    Texture,
};

[[nodiscard]] auto parameterType(const MaterialValue& value) -> ParameterType;

/// @brief The type's name in template files and errors: "int", "uint", "float", "vec2", "vec3", "vec4", "texture".
[[nodiscard]] auto parameterTypeName(ParameterType type) -> std::string_view;

/// @brief A texture parameter's default in a template file: one of DefaultAssets' solid colours.
enum class DefaultTexture
{
    White,
    Black,
    Normal,
};

/// @brief The textures DefaultTexture names, which MaterialTemplate::create resolves a template's defaults to.
struct DefaultTextures
{
    std::shared_ptr<Texture> white;
    std::shared_ptr<Texture> black;
    std::shared_ptr<Texture> normal;
};

/// @brief A parameter's default as a template file gives it: MaterialValue with a DefaultTexture for a texture.
using ParameterDefault = std::variant<std::int32_t, std::uint32_t, float, math::Float2, math::Float3, math::Float4, DefaultTexture>;

struct TemplateParameter
{
    std::string name;
    ParameterDefault defaultValue;
};

/// @brief A material template as its file describes it (serialize::parseTemplate): its surface shader and its
///        parameters, in the order of the surface shader's parameter block.
struct MaterialTemplateDesc
{
    std::string surface;
    std::vector<TemplateParameter> parameters;
};

/// @brief One field of a surface shader's parameter block, as the shader's reflection describes it.
struct BlockField
{
    std::string name;
    std::size_t offset{ 0 };
    std::size_t size{ 0 };
    std::optional<ParameterType> type; // None: a type no material parameter has
};

/// @brief A surface shader's parameter block as the build reflects it (shaders/surfaces/<surface>.layout.json, read
///        by serialize::parseSurfaceLayout).
struct SurfaceBlock
{
    std::vector<BlockField> fields;
    std::size_t size{ 0 };
};

/// @brief Where a parameter lives in a material's parameter block, laid out with scalar rules
///        (C++ packing, as the new stack's shaders read blocks; see shaders/modules/README.md).
///        A texture takes 8 bytes: its SampledImageDescriptor index, then its SamplerDescriptor index.
struct MaterialParameter
{
    std::size_t offset;
    std::size_t size;
    MaterialValue defaultValue;
};

/// @brief The parameters a kind of material has, their defaults and their block layout, and the surface shader
///        that reads them. Pipelines are not part of it: they belong to the render passes that draw the material,
///        each with its technique built for the template's surface shader.
class MaterialTemplate
{
public:
    /// @param surface The surface shader's name: shaders/surfaces/<surface>.slang, which every technique is built
    ///        with (<technique>.<surface>.spv).
    explicit MaterialTemplate(std::string surface = {}, MaterialType type = MaterialType::Opaque) :
        m_surface{ std::move(surface) },
        m_type{ type }
    {
    }

    /// @brief The template 'desc' describes, its texture defaults resolved to 'textures', checked against its surface
    ///        shader's parameter block (checkBlock).
    /// @return ErrorCode::InvalidData, naming the parameter, if 'desc' lists one twice, a texture default is null or
    ///         the parameters do not match 'block'.
    [[nodiscard]] static auto create(const MaterialTemplateDesc& desc, const SurfaceBlock& block, const DefaultTextures& textures)
        -> std::expected<MaterialTemplate, Error>;

    /// @brief Appends a parameter after the existing ones.
    /// @return ErrorCode::InvalidArgument if 'name' exists or a texture default is null.
    [[nodiscard]] auto addParameter(const std::string& name, MaterialValue defaultValue) -> std::expected<void, Error>;

    [[nodiscard]] auto surface() const noexcept -> const std::string& { return m_surface; }
    [[nodiscard]] auto type() const noexcept -> MaterialType { return m_type; }
    [[nodiscard]] auto parameterSize() const noexcept -> std::size_t { return m_parameterSize; }
    [[nodiscard]] auto hasParameter(const std::string& name) const -> bool { return m_parameters.contains(name); }

    /// @return nullptr if the template has no parameter 'name'.
    [[nodiscard]] auto parameter(const std::string& name) const -> const MaterialParameter*;

    /// @brief Whether 'value' may override parameter 'name': the parameter exists, 'value' holds the
    ///        type of its default, and a texture is not null.
    /// @return ErrorCode::InvalidArgument otherwise.
    [[nodiscard]] auto check(const std::string& name, const MaterialValue& value) const -> std::expected<void, Error>;

    /// @brief Whether the parameters lay out like 'block', the surface shader's parameter block: the same names in
    ///        the same order, each at its offset with its size and type, and the same total size.
    /// @return ErrorCode::InvalidData at the first difference, naming the parameter or the shader's field.
    [[nodiscard]] auto checkBlock(const SurfaceBlock& block) const -> std::expected<void, Error>;

    [[nodiscard]] auto parameters() const noexcept -> const std::unordered_map<std::string, MaterialParameter>&
    {
        return m_parameters;
    }

private:
    std::string m_surface;
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

    /// @brief Replaces every override with 'overrides': the parameters it leaves out take the template's default again
    ///        (the editor reapplying a material file).
    /// @return ErrorCode::InvalidArgument, leaving the material unchanged, if MaterialTemplate::check rejects a value.
    [[nodiscard]] auto setOverrides(std::unordered_map<std::string, MaterialValue> overrides) -> std::expected<void, Error>;

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
