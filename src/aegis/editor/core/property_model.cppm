export module aegis.editor.core:property_model;
import std;
import aegis.reflect;

export namespace aegis::editor
{
/// @brief The widget the inspector draws for a field.
enum class PropertyWidget
{
    Checkbox,  ///< A Bool
    Drag,      ///< An Int, UInt, Float, Double or Vec2/3/4: one drag per component
    Slider,    ///< A number between min and max
    Angle,     ///< A Float or Double in radians, shown and edited in degrees
    Color,     ///< A Vec3 or Vec4, as a linear color
    Rotation,  ///< A Quat, shown and edited as Euler angles in degrees
    Combo,     ///< An Enum: one of its named values
    Text,      ///< A String
    AssetSlot, ///< An AssetRef: the asset's name and a picker of the field's asset type
};

/// @brief How the inspector shows one field: its kind and metadata (reflect::FieldMeta) mapped to a widget.
struct PropertyRow
{
    const reflect::FieldInfo* field{ nullptr };
    std::string label; ///< The field's name as words ("nearPlane" -> "Near Plane")
    PropertyWidget widget{ PropertyWidget::Drag };
    int components{ 1 }; ///< The values the widget edits: 2 to 4 for a vector or a color, 3 for a rotation
    /// @brief Limits and drag step in the widget's unit: degrees for Angle and Rotation, the field's own otherwise.
    std::optional<double> min;
    std::optional<double> max;
    double step{ 1.0 };
    bool readOnly{ false };
};

/// @brief A field name as the inspector's label: camelCase and snake_case split into capitalized words.
[[nodiscard]] auto fieldLabel(std::string_view name) -> std::string;

/// @brief The row of 'field'; it points to 'field', so it lives as long as the field's TypeInfo.
[[nodiscard]] auto propertyRow(const reflect::FieldInfo& field) -> PropertyRow;

/// @brief The rows of the fields of 'type', in registration order.
[[nodiscard]] auto propertyRows(const reflect::TypeInfo& type) -> std::vector<PropertyRow>;
}
