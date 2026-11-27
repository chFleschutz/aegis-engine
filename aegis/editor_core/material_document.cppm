export module aegis.editor.core:material_document;
import std;
import aegis.reflect;
import aegis.serialize;
import :error;

export namespace aegis::editor
{
/// @brief A parameter of a material's template as the Material panel shows it: the kind of its value (Int, UInt, Float,
///        Vec2, Vec3, Vec4, or AssetRef for a texture) and the default a material file without it takes. A texture's
///        default is 0: the template's solid colour, which no asset ID names.
struct MaterialParameterInfo
{
    std::string name;
    reflect::FieldKind kind{ reflect::FieldKind::Float };
    reflect::Value defaultValue;
};

/// @brief A material file (`.material`) the editor edits: the file as it is now, and as it was last read or saved.
class MaterialDocument
{
public:
    /// @param path The file's path in its asset root, with '/' (for titles).
    MaterialDocument(std::uint64_t asset, std::string path, serialize::MaterialFile file);

    /// @brief The material's asset ID.
    [[nodiscard]] auto asset() const -> std::uint64_t { return m_asset; }
    [[nodiscard]] auto path() const -> const std::string& { return m_path; }
    /// @brief The file's name without its folder ("brick.material").
    [[nodiscard]] auto fileName() const -> std::string_view;
    [[nodiscard]] auto file() const -> const serialize::MaterialFile& { return m_file; }

    /// @brief The value the file gives the parameter 'name'; null when it has none (the template's default).
    [[nodiscard]] auto parameter(std::string_view name) const -> const reflect::Node*;
    /// @brief Gives the parameter 'name' the value 'value', or removes it from the file for none.
    /// @return Whether the file changed.
    auto setParameter(std::string_view name, std::optional<reflect::Node> value) -> bool;

    /// @brief Whether the file differs from the one last read or saved, by value (sameParameter).
    [[nodiscard]] auto dirty() const -> bool;
    /// @brief The file was written: it is the saved one from now on.
    void markSaved();
    /// @brief Back to the file last read or saved (the unsaved-changes prompt's Don't Save).
    /// @return Whether the file changed.
    auto revert() -> bool;

private:
    std::uint64_t m_asset;
    std::string m_path;
    serialize::MaterialFile m_file;
    serialize::MaterialFile m_saved;
};

/// @brief The material files the editor has opened in this session, each with its edits, by asset ID. They outlive the
///        scenes: a material belongs to the project, and its edits stay until they are saved or discarded.
class MaterialDocuments
{
public:
    /// @brief Adds the file of 'asset' as read from 'path', or returns the one opened before, which keeps its edits.
    auto open(std::uint64_t asset, std::string path, serialize::MaterialFile file) -> MaterialDocument&;
    /// @return Null if 'asset' was not opened.
    [[nodiscard]] auto find(std::uint64_t asset) -> MaterialDocument*;
    [[nodiscard]] auto find(std::uint64_t asset) const -> const MaterialDocument*;

    /// @brief Whether any file has unsaved changes (MaterialDocument::dirty).
    [[nodiscard]] auto dirty() const -> bool;
    /// @brief The files with unsaved changes, by asset ID.
    [[nodiscard]] auto unsaved() -> std::vector<MaterialDocument*>;
    /// @brief Reverts every file with unsaved changes (MaterialDocument::revert).
    /// @return The asset IDs of the files that changed.
    auto revert() -> std::vector<std::uint64_t>;

private:
    std::map<std::uint64_t, MaterialDocument> m_documents;
};

/// @brief The value the Material panel shows for 'parameter': the file's, or the default without one (or for a value
///        that does not read as the parameter's kind).
[[nodiscard]] auto parameterValue(const MaterialDocument& document, const MaterialParameterInfo& parameter) -> reflect::Value;

/// @brief The file's value for 'value' of 'parameter'; none for a texture of 0 (None, which removes it from the file).
/// @return ReflectFailed if 'value' is not of the parameter's kind.
[[nodiscard]] auto parameterNode(const MaterialParameterInfo& parameter, const reflect::Value& value)
    -> std::expected<std::optional<reflect::Node>, Error>;

/// @brief Whether two file values mean the same (none for a value not in the file): numbers by value, so a hand-written
///        "0.50" is the editor's 0.5.
[[nodiscard]] auto sameParameter(const std::optional<reflect::Node>& a, const std::optional<reflect::Node>& b) -> bool;

/// @brief The registered type the Material panel draws the parameters with (the inspector's property grid): a field
///        per parameter in their order, read from a MaterialDocument (parameterValue). A Vec3 is a colour, a number
///        drags by 0.01 and a texture is an asset slot of "Texture". It has no setter: edits go through editMaterial.
[[nodiscard]] auto materialType(std::span<const MaterialParameterInfo> parameters) -> reflect::TypeInfo;

/// @brief The path of a new material file in 'folder' (with '/', empty for the root): "New Material.material", or
///        "New Material 2.material" and up while 'taken' says the path is in use.
[[nodiscard]] auto newMaterialPath(std::string_view folder, const std::function<bool(std::string_view path)>& taken) -> std::string;
}
